#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <string.h>
#include "../spthread/spthread.h"
#include "../utils/Vec.h"
#include "k_proc.h"
#include "k_scheduler.h"

extern fat_st* current_fs;

// The system-wide scheduler data
scheduler_t* scheduler = NULL;

scheduler_t* k_get_scheduler() {
  if (scheduler == NULL) {
    k_scheduler_init();
  }
  return scheduler;
}

/*!
 * k_scheduler_init
 * Configure alarm signals, set up priority queues, etc.
 */
void k_scheduler_init() {
  scheduler = (scheduler_t*)malloc(sizeof(scheduler_t));
  if (scheduler == NULL) {
    panic("k_scheduler_init: malloc failed");
  }

  // Init the doubly-linked list(a deque) for the processes with differen
  // priorities
  for (int i = 0; i <= 3; i++) {
    scheduler->queues[i] = NULL;
    proc_queue_t* q = create_proc_queue();
    if (q == NULL) {
      panic("k_scheduler_init: malloc failed");
      for (int j = 0; j < i; j++) {  // Changed 'i++' to 'j++'
        free_proc_queue(scheduler->queues[j]);
      }
      free(scheduler);
      return;
    }
    scheduler->queues[i] = q;
  }

  scheduler->curr_proc = NULL;
  scheduler->init_proc = NULL;
  scheduler->current_priority = 0;
  scheduler->tick_count = 0;
  scheduler->waiting_dependency_vec = vec_new(0, free);
  scheduler->signal_dependency_vec = vec_new(0, free);
  scheduler->sleeping_vec = vec_new(0, NULL);
  if (scheduler->waiting_dependency_vec.data == NULL ||
      scheduler->signal_dependency_vec.data == NULL) {
    if (scheduler->waiting_dependency_vec.data != NULL) {
      vec_destroy(&scheduler->waiting_dependency_vec);
    }
    if (scheduler->signal_dependency_vec.data != NULL) {
      vec_destroy(&scheduler->signal_dependency_vec);
    }
    for (int i = 0; i < 4; i++) {
      if (scheduler->queues[i] != NULL) {
        free_proc_queue(scheduler->queues[i]);
      }
    }
    free(scheduler);
    panic("k_scheduler_init: vec_new failed");
  }
}

void k_scheduler_tick() {
  if (scheduler == NULL) {
    panic("k_scheduler_tick: scheduler is NULL");
  }

  // mask for while scheduler is waiting for
  // alarm to go off
  sigset_t suspend_set;
  sigfillset(&suspend_set);
  sigdelset(&suspend_set, SIGALRM);

  // just to make sure that
  // sigalrm doesn't terminate the process
  struct sigaction act = (struct sigaction){
      .sa_handler = k_alarm_handler,
      .sa_mask = suspend_set,
      .sa_flags = SA_RESTART,
  };
  sigaction(SIGALRM, &act, NULL);

  // make sure SIGALRM is unblocked
  sigset_t alarm_set;
  sigemptyset(&alarm_set);
  sigaddset(&alarm_set, SIGALRM);
  pthread_sigmask(SIG_UNBLOCK, &alarm_set, NULL);

  struct itimerval it;
  it.it_interval = (struct timeval){.tv_usec = centisecond * 10};
  it.it_value = it.it_interval;
  setitimer(ITIMER_REAL, &it, NULL);

  while (true) {
    k_scheduler_polling_signal_dependency();

    k_scheduler_polling_waiting_dependency();

    k_scheduler_reap_orphan_processes();

    k_scheduler_polling_sleeping_time();

    if (scheduler->queues[0]->size == 0 && scheduler->queues[1]->size == 0 &&
        scheduler->queues[2]->size == 0) {
      sigsuspend(&suspend_set);
      scheduler->tick_count++;
      continue;
    }

    int next_priority =
        k_schedule_rounds[scheduler->tick_count % k_schedule_rounds_size];
    // terminal_logger_debug("Scheduler tick: %lu, next priority: %d\n",
    //                         scheduler->tick_count, next_priority);
    pcb_t* next_proc = k_scheduler_dequeue(next_priority);
    if (next_proc != NULL) {
      if (next_proc->state != PROC_SUSPENDED) {
        k_scheduler_enqueue(next_proc);
      } else {
        // Continue the process
        scheduler->curr_proc = next_proc;
        // terminal_logger_debug("Continuing process %d\n",
        //                         scheduler->curr_proc->proc_id);
        if (k_proc_continue(scheduler->curr_proc) != 0) {
          terminal_logger_debug("Process %d  failed to continue\n",
                                scheduler->curr_proc->proc_id);
          panic("k_scheduler_tick: spthread_continue failed\n");
        }
      }
      sigsuspend(&suspend_set);
    } else {
      // No process to run, continue to the next tick
      scheduler->curr_proc = NULL;
    }

    if (scheduler->curr_proc != NULL) {
      if (scheduler->curr_proc->state == PROC_RUNNING) {
        if (k_proc_suspend(scheduler->curr_proc) != 0) {
          terminal_logger_debug("Process %d  failed to suspend\n",
                                scheduler->curr_proc->proc_id);
        }
      } else if (scheduler->curr_proc->state == PROC_ZOMBIE) {
        terminal_logger_debug("Process %d is a zombie\n",
                              scheduler->curr_proc->proc_id);
        k_scheduler_remove_pcb_by_pid(scheduler->curr_proc->proc_id,
                                      scheduler->curr_proc->priority);
        k_scheduler_remove_pcb_by_pid(scheduler->curr_proc->proc_id, 3);
        if (scheduler->curr_proc->proc_id == 1) {
          // If the current process is the init process, we need to
          // terminate the scheduler
          terminal_logger_debug("Init process %d is a zombie, terminating\n",
                                scheduler->curr_proc->proc_id);
          k_scheduler_clean_up();
          exit(0);
        }
      }
      k_scheduler_enqueue(scheduler->curr_proc);
    }
    scheduler->tick_count++;
  }
}

/*!
 * Put a process into the correct priority queue so that the scheduler can
 * find it.
 */
void k_scheduler_enqueue(pcb_t* proc) {
  if (proc == NULL) {
    spthread_enable_interrupts_self();
    return;
  }

  proc_node_t* node = create_node_from_proc(proc);
  if (node == NULL) {
    panic("k_scheduler_enqueue: create_node_from_proc failed");
  }

  if (proc->state == PROC_SUSPENDED) {
    push_back(scheduler->queues[proc->priority], node);
  } else {
    push_back(scheduler->queues[3], node);
  }
}

pcb_t* k_scheduler_dequeue(int priority_level) {
  if (priority_level < 0 || priority_level > 3) {
    return NULL;
  }
  if (scheduler == NULL || scheduler->queues[priority_level] == NULL) {
    return NULL;
  }

  proc_node_t* front_node = pop_front(scheduler->queues[priority_level]);
  if (front_node == NULL) {
    return NULL;
  }
  pcb_t* proc = front_node->proc;
  free_proc_node(front_node);
  return proc;
}

/*!
 * Lookup a process by PID.
 */
pcb_t* k_scheduler_get_proc_by_pid(pid_t pid) {
  if (scheduler == NULL) {
    return NULL;
  }

  for (int i = 0; i < 4; i++) {
    if (scheduler->queues[i] == NULL) {
      continue;
    }
    proc_node_t* node = find_node_by_pid(scheduler->queues[i], pid);
    if (node != NULL) {
      return node->proc;
    }
  }

  if (scheduler->curr_proc != NULL && scheduler->curr_proc->proc_id == pid) {
    return scheduler->curr_proc;
  }
  return NULL;
}

pcb_t* k_scheduler_remove_pcb_by_pid(pid_t pid, int priority_level) {
  if (scheduler == NULL || priority_level < 0 || priority_level > 3) {
    return NULL;
  }

  proc_node_t* node =
      remove_node_by_pid(scheduler->queues[priority_level], pid);
  if (node != NULL) {
    pcb_t* proc = node->proc;
    free_proc_node(node);
    return proc;
  }
  return NULL;
}

pcb_t* k_scheduler_get_current_proc() {
  return scheduler->curr_proc;
}

void k_alarm_handler(int signum) {
  (void)signum;  // Unused parameter
  // terminal_logger_debug("Alarm handler called\n");
}

void k_schedule(void) {
  k_scheduler_tick();
}

bool k_scheduler_register_waiting_dependency(pcb_t* waiting_proc,
                                             pcb_t* waited_proc) {
  if (waiting_proc == NULL || waited_proc == NULL) {
    return false;
  }

  waiting_dependency_t* dep =
      (waiting_dependency_t*)malloc(sizeof(waiting_dependency_t));
  if (dep == NULL) {
    panic("k_scheduler_register_waiting_dependency: malloc failed");
  }

  dep->waiting_proc = waiting_proc;
  dep->waited_proc = waited_proc;
  vec_push_back(&scheduler->waiting_dependency_vec, dep);
  waiting_proc->state = PROC_BLOCKED;
  return true;
}

bool k_scheduler_unregister_waiting_dependency(pcb_t* waiting_proc,
                                               pcb_t* waited_proc) {
  if (waiting_proc == NULL || waited_proc == NULL) {
    return false;
  }

  for (int i = 0; i < vec_len(&scheduler->waiting_dependency_vec); i++) {
    waiting_dependency_t* dep =
        (waiting_dependency_t*)scheduler->waiting_dependency_vec.data[i];
    if (dep->waiting_proc == waiting_proc && dep->waited_proc == waited_proc) {
      vec_erase(&scheduler->waiting_dependency_vec, i);
      return true;
    }
  }
  return false;
}

void k_scheduler_polling_waiting_dependency() {
  if (scheduler == NULL || vec_len(&scheduler->waiting_dependency_vec) == 0) {
    return;
  }

  // Vec enqueue_vec = vec_new(0, free);
  // Iterate backward to safely remove dependencies in place
  for (int i = vec_len(&scheduler->waiting_dependency_vec) - 1; i >= 0; i--) {
    waiting_dependency_t* dep =
        (waiting_dependency_t*)vec_get(&scheduler->waiting_dependency_vec, i);

    if (dep->waited_proc->state == PROC_ZOMBIE) {
      // Unblock the waiting process
      pcb_t* waiting_proc = dep->waiting_proc;
      waiting_proc->state = PROC_SUSPENDED;
      pcb_t* removed_proc =
          k_scheduler_remove_pcb_by_pid(waiting_proc->proc_id, 3);
      if (removed_proc == NULL ||
          removed_proc->proc_id != waiting_proc->proc_id) {
        terminal_logger_debug("Failed to remove waiting process %d\n",
                              waiting_proc->proc_id);
        return;
      } else {
        terminal_logger_debug(
            "Removed waiting process %d from the third queue\n",
            waiting_proc->proc_id);
      }
      terminal_logger_debug("Unblocking waiting process %d and enqueueing\n",
                            waiting_proc->proc_id);
      k_scheduler_remove_pcb_by_pid(waiting_proc->proc_id, 3);
      k_scheduler_enqueue(waiting_proc);

      // Free all equals waiting process the dependency structure and remove it
      // from the vector
      for (int j = 0; j < vec_len(&scheduler->waiting_dependency_vec); j++) {
        waiting_dependency_t* waiting_dep = (waiting_dependency_t*)vec_get(
            &scheduler->waiting_dependency_vec, j);
        if (waiting_dep->waiting_proc == waiting_proc) {
          vec_erase(&scheduler->waiting_dependency_vec, j);
        }
      }
    }
  }

  // Enqueue all waiting processes that were unblocked
  // for (int i = 0; i < vec_len(&enqueue_vec); i++) {
  //   pcb_t* proc = (pcb_t*)vec_get(&enqueue_vec, i);
  //   k_scheduler_enqueue(proc);
  //   terminal_logger_debug("Enqueueing waiting process %d\n",
  //   proc->proc_id);
  // }
  // terminal_logger_debug("Finished polling waiting dependencies\n");
  // fflush(stderr);
}

void k_scheduler_polling_signal_dependency() {
  if (scheduler == NULL || vec_len(&scheduler->signal_dependency_vec) == 0) {
    return;
  }

  // Iterate backward to safely remove dependencies in place
  for (int i = vec_len(&scheduler->signal_dependency_vec) - 1; i >= 0; i--) {
    signal_dependency_t* dep =
        (signal_dependency_t*)vec_get(&scheduler->signal_dependency_vec, i);
    terminal_logger_debug(
        "k_scheduler_polling_signal_dependency: Process %d received signal %d "
        "from Process %d\n",
        dep->receiver->proc_id, dep->signal_num, dep->sender->proc_id);
    logger_log_life_cycle_event(k_get_scheduler()->tick_count, "SIGNALED",
                                dep->receiver->proc_id, dep->receiver->priority,
                                ((char**)dep->receiver->argv)[0]);
    k_proc_kill(dep->signal_num, dep->receiver);

    // For P_SIGSTOP, check the waiting process
    if (dep->signal_num == P_SIGSTOP) {
      int waiting_len = vec_len(&scheduler->waiting_dependency_vec);
      int target_index = -1;
      for (int i = 0; i < waiting_len; i++) {
        waiting_dependency_t* waiting_dep = (waiting_dependency_t*)vec_get(
            &scheduler->waiting_dependency_vec, i);
        if (waiting_dep->waiting_proc == dep->sender &&
            waiting_dep->waited_proc == dep->receiver) {
          target_index = i;
          break;
        }
      }
      // Found the waiting dependency where the waited process is the signal
      // receiver and the waiting process is the signal sender, remove it from
      // the waiting vec, continue the waiting process
      if (target_index != -1) {
        waiting_dependency_t* waiting_dep = (waiting_dependency_t*)vec_get(
            &scheduler->waiting_dependency_vec, target_index);
        waiting_dep->waiting_proc->state = PROC_SUSPENDED;
        k_scheduler_remove_pcb_by_pid(waiting_dep->waiting_proc->proc_id, 3);
        k_scheduler_remove_pcb_by_pid(waiting_dep->waiting_proc->proc_id,
                                      waiting_dep->waiting_proc->priority);
        k_scheduler_enqueue(waiting_dep->waiting_proc);
        terminal_logger_debug("Unblocking waiting process %d and enqueueing\n",
                              waiting_dep->waiting_proc->proc_id);
        vec_erase(&scheduler->waiting_dependency_vec, target_index);
      } else {
        terminal_logger_debug("No matching waiting dependency found\n");
      }
    }

    vec_erase(&scheduler->signal_dependency_vec, i);
  }
}

void k_scheduler_print_queue() {
  if (scheduler == NULL) {
    return;
  }

  terminal_logger_debug("Scheduler Queue:\n");
  for (int i = 0; i < 4; i++) {
    terminal_logger_debug("Priority %d: ", i);
    proc_node_t* node = front(scheduler->queues[i]);
    while (node != NULL) {
      terminal_logger_debug("%d ", node->proc->proc_id);
      node = node->next;
    }
    terminal_logger_debug("\n");
  }
  if (scheduler->curr_proc == NULL) {
    terminal_logger_debug("No current process\n");
  } else {
    terminal_logger_debug("Current Process: %d\n",
                          scheduler->curr_proc->proc_id);
  }
  terminal_logger_debug("Waiting Dependencies: %lu\n",
                        vec_len(&scheduler->waiting_dependency_vec));
  for (int i = 0; i < vec_len(&scheduler->waiting_dependency_vec); i++) {
    waiting_dependency_t* dep =
        (waiting_dependency_t*)vec_get(&scheduler->waiting_dependency_vec, i);
    terminal_logger_debug("Waiting: %d, Waited: %d\n",
                          dep->waiting_proc->proc_id,
                          dep->waited_proc->proc_id);
  }

  terminal_logger_debug("Signal Dependencies: %lu\n",
                        vec_len(&scheduler->signal_dependency_vec));
  for (int i = 0; i < vec_len(&scheduler->signal_dependency_vec); i++) {
    signal_dependency_t* dep =
        (signal_dependency_t*)vec_get(&scheduler->signal_dependency_vec, i);
    terminal_logger_debug("Sender: %d, Receiver: %d, Signal: %d\n",
                          dep->sender->proc_id, dep->receiver->proc_id,
                          dep->signal_num);
  }
  terminal_logger_debug("Sleeping Processes: %lu\n",
                        vec_len(&scheduler->sleeping_vec));
  for (int i = 0; i < vec_len(&scheduler->sleeping_vec); i++) {
    sleeping_scheduler_t* sleep_time =
        (sleeping_scheduler_t*)vec_get(&scheduler->sleeping_vec, i);
    terminal_logger_debug("Sleeping Process: %d, Remaining Ticks: %lu\n",
                          sleep_time->sleeping_proc->proc_id,
                          sleep_time->remaining_ticks);
  }
  terminal_logger_debug("------------------------\n");
  fflush(stderr);
}

pcb_t* k_scheduler_get_init() {
  if (scheduler == NULL) {
    return NULL;
  }
  return scheduler->init_proc;
}

void k_scheduler_set_init(pcb_t* init_proc) {
  if (scheduler == NULL) {
    return;
  }
  scheduler->init_proc = init_proc;
}

pcb_t* k_scheduler_get_pcb_from_queue(proc_queue_t* queue, pid_t pid) {
  if (queue == NULL) {
    return NULL;
  }
  proc_node_t* node = find_node_by_pid(queue, pid);
  if (node != NULL) {
    return node->proc;
  }
  return NULL;
}

void k_scheduler_reap_orphan_processes() {
  pcb_t* init_pcb = k_scheduler_get_init();
  if (init_pcb == NULL) {
    return;
  }

  for (int i = vec_len(&init_pcb->children_proc_ids) - 1; i >= 0; i--) {
    pid_t child_pid = (pid_t)(intptr_t)vec_get(&init_pcb->children_proc_ids, i);
    if (child_pid == -1) {
      continue;
    }
    pcb_t* child_pcb = k_scheduler_get_proc_by_pid(child_pid);
    // terminal_logger_debug("Checking child process %d\n", child_pid);
    int wstatus;
    int wait_result =
        k_proc_wait(k_scheduler_get_current_proc(), child_pcb, &wstatus, 1);
    if (wait_result == -1) {
      // Error waiting for child process or the child process is not a zombie
      // terminal_logger_debug("Error waiting for child process %d\n",
      // child_pid);
    } else {
      // terminal_logger_debug("Child process %d waited by init\n",
      // child_pid);
      if (P_WIFEXITED(wstatus) || P_WIFSIGNALED(wstatus)) {
        vec_erase(&init_pcb->children_proc_ids, i);
        terminal_logger_debug("Child process %d exited\n", child_pid);
      } else if (P_WIFSTOPPED(wstatus)) {
        terminal_logger_debug("Child process %d stopped\n", child_pid);
      }
    }
  }
}

long k_scheduler_get_ticks() {
  return scheduler->tick_count;
}

void k_scheduler_polling_sleeping_time() {
  if (scheduler == NULL) {
    return;
  }
  if (vec_len(&scheduler->sleeping_vec) == 0) {
    // terminal_logger_debug("No sleeping processes to wake up\n");
    return;
  }

  for (int i = 0; i < vec_len(&scheduler->sleeping_vec); i++) {
    // terminal_logger_debug(
    //     "k_scheduler_polling_sleeping_time: scheduler tick count: %lu\n",
    //     scheduler->tick_count);
    sleeping_scheduler_t* sleep_time =
        (sleeping_scheduler_t*)vec_get(&scheduler->sleeping_vec, i);
    pcb_t* proc = sleep_time->sleeping_proc;
    if (proc != NULL && proc->state == PROC_BLOCKED) {
      sleep_time->remaining_ticks--;
      // terminal_logger_debug("Sleeping process %d remaining ticks: %d\n",
      //                       proc->proc_id, sleep_time->remaining_ticks);
      if (sleep_time->remaining_ticks <= 0) {
        // Remove the sleeping process from the vector
        vec_erase(&scheduler->sleeping_vec, i);
        i--;
        // Unblock the sleeping process
        terminal_logger_debug("Waking up sleeping process %d\n", proc->proc_id);
        proc->state = PROC_SUSPENDED;
        k_scheduler_remove_pcb_by_pid(proc->proc_id, proc->priority);
        k_scheduler_remove_pcb_by_pid(proc->proc_id, 3);
        k_scheduler_enqueue(proc);
        terminal_logger_debug("Enqueueing process %d with state %d\n",
                              proc->proc_id, proc->state);
      }
    }
    if (proc != NULL && proc->state == PROC_STOPPED) {
      // Remove the sleeping process from the vector
      // vec_erase(&scheduler->sleeping_vec, i);
      // i--;
      // k_scheduler_remove_pcb_by_pid(proc->proc_id, proc->priority);
      // k_scheduler_remove_pcb_by_pid(proc->proc_id, 3);
      // k_scheduler_enqueue(proc);
      // terminal_logger_debug("Enqueueing process %d with state %d\n",
      //                       proc->proc_id, proc->state);
      // terminal_logger_debug("Sleeping process %d is stopped\n",
      // proc->proc_id);
    }
  }
  spthread_enable_interrupts_self();
}

bool k_scheduler_register_signal(pid_t sender_pid,
                                 pid_t target_pid,
                                 int signal_num) {
  terminal_logger_debug(
      "k_scheduler_register_signal: sender_pid: %d, target_pid: %d, "
      "signal_num: %d\n",
      sender_pid, target_pid, signal_num);

  if (signal_num != P_SIGCONT && signal_num != P_SIGSTOP &&
      signal_num != P_SIGTERM) {
    terminal_logger_debug("k_scheduler_register_signal: Invalid signal %d\n",
                          signal_num);
    return false;
  }

  signal_dependency_t* dep =
      (signal_dependency_t*)malloc(sizeof(signal_dependency_t));
  if (dep == NULL) {
    return false;
  }

  pcb_t* sender_pcb = k_scheduler_get_proc_by_pid(2);
  if (sender_pcb == NULL) {
    const char* err_msg = "Invalid signal sender process.\n";
    k_write(current_fs, STDERR_FILENO, err_msg, strlen(err_msg));
    free(dep);
    return false;
  }
  pcb_t* receiver_pcb = k_scheduler_get_proc_by_pid(target_pid);
  if (receiver_pcb == NULL) {
    const char* err_msg = "Invalid signal target process.\n";
    k_write(current_fs, STDERR_FILENO, err_msg, strlen(err_msg));
    free(dep);
    return false;
  }

  dep->sender = sender_pcb;
  dep->receiver = receiver_pcb;
  dep->signal_num = signal_num;

  vec_push_back(&scheduler->signal_dependency_vec, dep);
  terminal_logger_debug(
      "k_scheduler_register_signal: Registered signal dependency: sender %d, "
      "receiver %d, signal %d\n",
      sender_pid, target_pid, signal_num);
  return true;
}

void k_scheduler_clean_up() {
  k_scheduler_reap_orphan_processes();

  for (int i = 0; i < 4; i++) {
    proc_queue_t* curr_queue = scheduler->queues[i];
    while (curr_queue->size != 0) {
      proc_node_t* curr_node = pop_front(curr_queue);
      // k_proc_sigterm(curr_node->proc);
      k_proc_cancel(curr_node->proc);
      k_proc_cleanup(curr_node->proc);
      free_proc_node(curr_node);
    }
    free_proc_queue(curr_queue);
    scheduler->queues[i] = NULL;
  }

  // TODO: Save all modified files in the FS and clean remaining FS resources

  while (vec_len(&scheduler->waiting_dependency_vec) != 0) {
    waiting_dependency_t* dep =
        (waiting_dependency_t*)vec_back(&scheduler->waiting_dependency_vec);
    if (dep == NULL) {
      break;
    }
    vec_pop_back(&scheduler->waiting_dependency_vec);
    free(dep);
  }
  vec_destroy(&scheduler->waiting_dependency_vec);

  while (vec_len(&scheduler->signal_dependency_vec) != 0) {
    signal_dependency_t* dep =
        (signal_dependency_t*)vec_back(&scheduler->signal_dependency_vec);
    if (dep == NULL) {
      break;
    }
    vec_pop_back(&scheduler->signal_dependency_vec);
    free(dep);
  }
  vec_destroy(&scheduler->signal_dependency_vec);

  while (vec_len(&scheduler->sleeping_vec) != 0) {
    sleeping_scheduler_t* dep =
        (sleeping_scheduler_t*)vec_back(&scheduler->sleeping_vec);
    if (dep == NULL) {
      break;
    }
    vec_pop_back(&scheduler->sleeping_vec);
    free(dep);
  }
  vec_destroy(&scheduler->sleeping_vec);
}