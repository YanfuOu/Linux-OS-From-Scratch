#include "k_proc.h"
#include <errno.h>
#include <string.h>
#include "k_file_system.h"
#include "k_scheduler.h"
#define FD_SIZE 1024

static pid_t g_next_pid = 1;  // used to assign unique PIDs
extern scheduler_t* scheduler;
extern fat_st* current_fs;
extern Vec proc_fd_table;

pcb_t* k_proc_create(void* (*function)(void*), void* argv) {
  // Allocate and initialize the PCB
  pcb_t* new_pcb = (pcb_t*)malloc(sizeof(pcb_t));
  if (!new_pcb) {
    panic("k_proc_create: could not allocate PCB");
  }

  new_pcb->proc_id = g_next_pid++;
  new_pcb->parent_proc_id =
      1;  // If this is a child, we might set it to parent's PID
  new_pcb->state = PROC_SUSPENDED;
  new_pcb->function = function;
  new_pcb->argv = argv;
  new_pcb->priority = 0;  // e.g. default priority
  new_pcb->exit_status = 0;
  new_pcb->children_proc_ids = vec_new(10, NULL);
  new_pcb->num_children = 0;
  new_pcb->exit_code = 0;

  // Create the underlying spthread but do *NOT* run it yet
  new_pcb->thread = (spthread_t*)malloc(sizeof(spthread_t));
  if (!new_pcb->thread) {
    panic("k_proc_create: could not allocate spthread_t");
  }

  // Create the spthread in SUSPENDED state. We pass the function we want to run
  // so that once we spthread_continue, it will begin running that function.
  int rc = spthread_create(new_pcb->thread,
                           NULL,      // default attributes
                           function,  // start routine
                           argv       // argument to the routine
  );
  if (rc != 0) {
    panic("k_proc_create: spthread_create failed");
  }

  return new_pcb;
}

int k_proc_suspend(pcb_t* pcb) {
  if (pcb->state == PROC_SUSPENDED) {
    return -1;  // Already suspended
  }
  // Suspend the thread
  int suspend_result = spthread_suspend(*pcb->thread);
  if (suspend_result == 0) {
    pcb->state = PROC_SUSPENDED;
  } else {
    terminal_logger_debug(
        "k_proc_suspend: spthread_suspend failed for process %d with state "
        "%d\n",
        pcb->proc_id, pcb->state);
    switch (suspend_result) {
      case EAGAIN:
        terminal_logger_debug("EAGAIN: The thread could not be signaled\n");
        break;
      case ENOSYS:
        terminal_logger_debug("ENOSYS: Not supported on this system\n");
        break;
      case ESRCH:
        terminal_logger_debug("ESRCH: The thread specified is not valid\n");
        break;
      default:
        terminal_logger_debug("Unknown error code: %d\n", suspend_result);
    }
    panic("k_proc_suspend: spthread_suspend failed");
  }
  return suspend_result;
}

int k_proc_continue(pcb_t* pcb) {
  if (pcb->state == PROC_RUNNING) {
    return -1;  // Already running
  }
  // change order: continue but the state remains the same
  pcb->state = PROC_RUNNING;
  int continue_result = spthread_continue(*pcb->thread);
  if (continue_result != 0) {
    terminal_logger_debug(
        "k_proc_continue: spthread_continue failed for process %d\n",
        pcb->proc_id);
    switch (continue_result) {
      case EAGAIN:
        terminal_logger_debug("EAGAIN: The thread could not be signaled\n");
        break;
      case ENOSYS:
        terminal_logger_debug("ENOSYS: Not supported on this system\n");
        break;
      case ESRCH:
        terminal_logger_debug("ESRCH: The thread specified is not valid\n");
        break;
      default:
        terminal_logger_debug("Unknown error code: %d\n", continue_result);
    }
    panic("k_proc_continue: spthread_continue failed");
  }
  return continue_result;
}

int k_proc_cancel(pcb_t* pcb) {
  if (pcb->state == PROC_ZOMBIE) {
    return -1;  // Already terminated
  }
  pcb->state = PROC_ZOMBIE;
  return spthread_cancel(*pcb->thread);
}

void k_proc_exit(pcb_t* pcb, void* status) {
  if (pcb->state == PROC_ZOMBIE) {
    return;  // Already terminated
  }

  // pcb->exit_status = (int)(intptr_t)status;
  pcb_t* init_pcb = k_scheduler_get_init();
  if (init_pcb == NULL) {
    spthread_enable_interrupts_self();
    return;
  }

  for (int i = vec_len(&pcb->children_proc_ids) - 1; i >= 0; i--) {
    intptr_t child_pid_intptr = (intptr_t)vec_get(&pcb->children_proc_ids, i);
    if (child_pid_intptr == 0) {
      terminal_logger_debug("k_proc_exit: vec_get returned NULL for index %d\n",
                            i);
      continue;
    }
    pid_t child_pid = (pid_t)child_pid_intptr;
    pcb_t* child_pcb = k_scheduler_get_proc_by_pid(child_pid);
    if (child_pcb != NULL) {
      // Migrate this child to the init process
      child_pcb->parent_proc_id = init_pcb->proc_id;
      vec_push_back(&init_pcb->children_proc_ids,
                    (ptr_t)(intptr_t)child_pcb->proc_id);
      terminal_logger_debug(
          "k_proc_exit: migrating child process %d of process %d to init "
          "process %d\n",
          child_pid, pcb->proc_id, init_pcb->proc_id);
      logger_log_life_cycle_event(k_scheduler_get_ticks(), "ORPHAN", child_pid,
                                  child_pcb->priority,
                                  ((char**)child_pcb->argv)[0]);
    }

    // Remove the child process from the parent process
    vec_erase(&pcb->children_proc_ids, i);
  }
  pcb->state = PROC_ZOMBIE;

  int global_fd_table_size = vec_len(&proc_fd_table);
  proc_table_entry_t* fd_entry;
  int index = -1;
  for (int j = 0; j < global_fd_table_size; j++) {
    fd_entry = (proc_table_entry_t*)vec_get(&proc_fd_table, j);
    if (fd_entry != NULL && fd_entry->proc_id == pcb->proc_id) {
      index = j;
      break;
    }
  }
  for (int i = 3; i < MAX_FDS_PER_PROCESS; i++) {
    if (fd_entry->fd_table.entries[i].file_index != -1) {
      terminal_logger_debug(
          "k_proc_exit: closing file descriptor %d for process %d\n", i,
          pcb->proc_id);
      k_close(current_fs, fd_entry->fd_table.entries[i].file_index);
    }
  }

  if (index != -1) {
    terminal_logger_debug(
        "k_proc_exit: removing file descriptor table at index %d for process "
        "%d\n",
        index, fd_entry->proc_id);
    vec_erase(&proc_fd_table, index);
  }

  logger_log_life_cycle_event(k_scheduler_get_ticks(), "ZOMBIE", pcb->proc_id,
                              pcb->priority, ((char**)pcb->argv)[0]);

  spthread_enable_interrupts_self();
  spthread_exit(status);
}

int k_proc_sigterm(pcb_t* pcb) {
  if (!pcb) {
    terminal_logger_debug("k_proc_sigterm: pcb is NULL\n");
    return -1;
  }
  if (pcb->state == PROC_ZOMBIE) {
    // No need to kill the process anymore
    terminal_logger_debug("k_proc_sigterm: process %d is already terminated\n",
                          pcb->proc_id);
    return 1;
  } else {
    terminal_logger_debug("k_proc_sigterm: killing process %d\n", pcb->proc_id);
  }

  pcb_t* init_pcb = k_scheduler_get_init();
  if (init_pcb == NULL) {
    return -1;
  }
  int length = vec_len(&pcb->children_proc_ids);
  terminal_logger_debug("k_proc_sigterm: killing process %d with %d children\n",
                        pcb->proc_id, length);

  for (int i = vec_len(&pcb->children_proc_ids) - 1; i >= 0; i--) {
    intptr_t child_pid_intptr = (intptr_t)vec_get(&pcb->children_proc_ids, i);
    if (child_pid_intptr == 0) {
      terminal_logger_debug(
          "k_proc_sigterm: vec_get returned NULL for index %d\n", i);
      continue;
    }
    pid_t child_pid = (pid_t)child_pid_intptr;
    pcb_t* child_pcb = k_scheduler_get_proc_by_pid(child_pid);
    if (child_pcb != NULL) {
      // Migrate this child to the init process
      child_pcb->parent_proc_id = init_pcb->proc_id;
      vec_push_back(&init_pcb->children_proc_ids,
                    (ptr_t)(intptr_t)child_pcb->proc_id);
      terminal_logger_debug(
          "k_proc_sigterm: migrating child process %d of process %d to init "
          "process %d\n",
          child_pid, pcb->proc_id, init_pcb->proc_id);
      logger_log_life_cycle_event(k_scheduler_get_ticks(), "ORPHAN", child_pid,
                                  child_pcb->priority,
                                  ((char**)child_pcb->argv)[0]);
    }
    // Remove the child process from the parent process
    vec_erase(&pcb->children_proc_ids, i);
    terminal_logger_debug(
        "k_proc_sigterm: removing child process %d from parent %d\n", child_pid,
        pcb->proc_id);
  }

  // Remove the process from the scheduler
  k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
  k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);

  logger_log_life_cycle_event(k_scheduler_get_ticks(), "ZOMBIE", pcb->proc_id,
                              pcb->priority, ((char**)pcb->argv)[0]);
  int cancel_result = spthread_cancel(*pcb->thread);
  if (cancel_result != 0) {
    terminal_logger_debug(
        "k_proc_sigterm: spthread_cancel for process %d failed\n",
        pcb->proc_id);
    // k_proc_cleanup(pcb);
    return -1;
  }

  // Clean up the process
  // k_proc_cleanup(pcb);
  pcb->state = PROC_ZOMBIE;

  // k_scheduler_enqueue(pcb);
  return cancel_result;
}

int k_proc_sigstop(pcb_t* pcb) {
  if (pcb == NULL || pcb->state == PROC_ZOMBIE) {
    return -1;
  }
  // If the state of the process is blocked, then do nothing.
  if (pcb->state == PROC_STOPPED) {
    return 0;
  }

  // If the state of the process is running, then directly suspend it and put it
  // into the 4th queue.
  // If the state of the process is suspended, then change its state to stopped,
  // and moving it from the current queue to the 4th queue.
  // If the state of the process is blocked, then just transfer its
  // state into stopped and then do nothing.
  if (pcb->state == PROC_RUNNING) {
    int suspend_result = k_proc_suspend(pcb);
    if (suspend_result != 0) {
      terminal_logger_debug("k_proc_sigstop: failed to suspend process %d\n",
                            pcb->proc_id);
      return -1;  // Failed to suspend
    } else {
      terminal_logger_debug("k_proc_sigstop: suspended process %d\n",
                            pcb->proc_id);
    }
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);
  } else if (pcb->state == PROC_SUSPENDED) {
    terminal_logger_debug("k_proc_sigstop: process %d already suspended\n",
                          pcb->proc_id);
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);
  } else if (pcb->state == PROC_BLOCKED) {
    terminal_logger_debug("k_proc_sigstop: process %d is blocked\n",
                          pcb->proc_id);
  }

  pcb->state = PROC_STOPPED;
  // k_scheduler_enqueue(pcb);
  terminal_logger_debug(
      "k_proc_sigstop: process %d is stopped and moved to the 4th queue\n",
      pcb->proc_id);
  return 0;
}

int k_proc_sigcont(pcb_t* pcb) {
  if (pcb == NULL || pcb->state == PROC_ZOMBIE) {
    return -1;
  }

  // For P_SIGCONT, check the sleeping process
  int sleeping_len = vec_len(&scheduler->sleeping_vec);
  terminal_logger_debug(
      "k_proc_sigcont: checking sleeping processes, length: %d\n",
      sleeping_len);
  int target_index = -1;
  for (int i = 0; i < sleeping_len; i++) {
    sleeping_scheduler_t* sleep_time =
        (sleeping_scheduler_t*)vec_get(&scheduler->sleeping_vec, i);
    if (sleep_time->sleeping_proc != NULL &&
        sleep_time->sleeping_proc->proc_id == pcb->proc_id) {
      target_index = i;
      break;
    }
  }
  // Found the sleeping process, add it back to the scheduler with its state
  // to blocking
  if (target_index != -1) {
    terminal_logger_debug(
        "k_proc_sigcont: found sleeping process %d in the scheduler\n",
        pcb->proc_id);
    pcb->state = PROC_BLOCKED;
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
    k_scheduler_enqueue(pcb);
    terminal_logger_debug("Unblocking sleeping process %d and enqueueing\n",
                          pcb->proc_id);
    // vec_erase(&scheduler->sleeping_vec, target_index);
  } else {
    terminal_logger_debug(
        "k_proc_sigcont: process %d is not in the sleeping queue\n",
        pcb->proc_id);
    pcb->state = PROC_SUSPENDED;
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);
    k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
    k_scheduler_enqueue(pcb);
    terminal_logger_debug(
        "k_proc_sigcont: process %d is moved to the %dth queue\n", pcb->proc_id,
        pcb->priority);
  }

  return 0;
}

int k_proc_kill(signal_t signal, pcb_t* pcb) {
  if (pcb == NULL) {
    return -1;
  }

  switch (signal) {
    case P_SIGTERM:
      terminal_logger_debug("k_proc_kill: sending P_SIGTERM to process %d\n",
                            pcb->proc_id);
      return k_proc_sigterm(pcb);
    case P_SIGSTOP:
      terminal_logger_debug("k_proc_kill: sending P_SIGSTOP to process %d\n",
                            pcb->proc_id);
      return k_proc_sigstop(pcb);
    case P_SIGCONT:
      terminal_logger_debug("k_proc_kill: sending P_SIGCONT to process %d\n",
                            pcb->proc_id);
      return k_proc_sigcont(pcb);
    default:
      return -1;  // Unsupported signal
  }
}

int k_proc_sleep(pcb_t* pcb, unsigned int ticks) {
  if (!pcb || ticks < 0) {
    return -1;
  }
  if (pcb->state == PROC_ZOMBIE) {
    terminal_logger_debug("k_proc_sleep: process %d is already terminated\n",
                          pcb->proc_id);
    return 1;
  }
  sleeping_scheduler_t* sleep_scheduler =
      (sleeping_scheduler_t*)malloc(sizeof(sleeping_scheduler_t));
  if (!sleep_scheduler) {
    return -1;  // Memory allocation failed
  }
  pcb->state = PROC_BLOCKED;
  sleep_scheduler->remaining_ticks = ticks;
  sleep_scheduler->sleeping_proc = pcb;
  vec_push_back(&scheduler->sleeping_vec, sleep_scheduler);
  // k_scheduler_remove_pcb_by_pid(pcb->proc_id, pcb->priority);
  // k_scheduler_remove_pcb_by_pid(pcb->proc_id, 3);
  // k_scheduler_enqueue(pcb);
  terminal_logger_debug("k_proc_sleep: process %d is sleeping for %d ticks\n",
                        pcb->proc_id, ticks);
  // We need to suspend the process here
  spthread_suspend_self();
  terminal_logger_debug("k_proc_sleep: process %d is woken up after %d ticks\n",
                        pcb->proc_id, ticks);
  pcb->state = PROC_ZOMBIE;
  return 0;
}

void k_proc_cleanup(pcb_t* pcb) {
  if (!pcb) {
    return;
  }
  pid_t pid = pcb->proc_id;
  // free the FD table
  pcb_t* curr = k_scheduler_get_proc_by_pid(pid);
  terminal_logger_debug("k_proc_cleanup: cleaning up process %d\n", pid);
  if (curr != NULL) {
    k_scheduler_remove_pcb_by_pid(pid, curr->priority);
    k_scheduler_remove_pcb_by_pid(pid, 3);
  }

  // free the spthread structure
  if (pcb->thread) {
    spthread_join(*pcb->thread, NULL);
    free(pcb->thread);
    pcb->thread = NULL;
  }

  // Free the fd table in the file system
  int global_fd_table_size = vec_len(&proc_fd_table);
  for (int i = 0; i < global_fd_table_size; i++) {
    proc_table_entry_t* fd_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, i);
    if (fd_entry != NULL && fd_entry->proc_id == pid) {
      vec_erase(&proc_fd_table, i);
      break;
    }
  }

  free(pcb->argv);
  vec_destroy(&pcb->children_proc_ids);
  free(pcb);

  terminal_logger_debug("k_proc_cleanup: cleaned up process %d\n", pid);
}

pid_t k_proc_get_pid(pcb_t* pcb) {
  if (!pcb)
    return -1;
  return pcb->proc_id;
}

int k_proc_wait(pcb_t* waiting_proc,
                pcb_t* waited_proc,
                int* wstatus,
                int flags) {
  if (!waiting_proc || !waited_proc) {
    return -1;
  }

  pid_t waiting_proc_id = waiting_proc->proc_id;
  pid_t waited_proc_id = waited_proc->proc_id;
  if (waiting_proc_id == waited_proc_id) {
    return -1;  // Cannot wait for itself
  }

  if (waited_proc->state == PROC_ZOMBIE) {
    // If the waited process is a zombie, we can just clean it up
    if (wstatus) {
      *wstatus = waited_proc->exit_code;
    }
    pcb_t* waited_proc_copy = k_scheduler_get_proc_by_pid(waited_proc->proc_id);
    if (waited_proc_copy && waited_proc_copy->state == PROC_ZOMBIE) {
      k_scheduler_remove_pcb_by_pid(waited_proc_copy->proc_id,
                                    waited_proc_copy->priority);
      k_scheduler_remove_pcb_by_pid(waited_proc_copy->proc_id, 3);
      k_proc_cleanup(waited_proc_copy);
      terminal_logger_debug(
          "k_proc_wait: waited process %d is a zombie, cleaning it up\n",
          waited_proc_id);
    }
    return 0;
  } else if (waited_proc->state == PROC_STOPPED) {
    terminal_logger_debug(
        "k_proc_wait: waited process %d is stopped, waiting for it to resume\n",
        waited_proc_id);
    *wstatus = P_STOPPED_CODE;
  } else if (waited_proc->state == PROC_RUNNING ||
             waited_proc->state == PROC_SUSPENDED) {
    // If the waited process is running, we can just return -1
    *wstatus = P_RUNNING_CODE;
    terminal_logger_debug(
        "k_proc_wait: waited process %d is running, cannot wait\n",
        waited_proc_id);
  } else if (waited_proc->state == PROC_BLOCKED) {
    *wstatus = P_BLOCKED_CODE;
    terminal_logger_debug(
        "k_proc_wait: waited process %d is blocked, cannot wait\n",
        waited_proc_id);
  }

  // Return for nohang
  if (flags == 1) {
    terminal_logger_debug("k_proc_wait: nohang flag is set, returning -1\n");
    return -1;
  }

  // For blocking wait, we marked the waiting process as blocked, and move it to
  // the fourth queue, register it and the waited process to the scheduler,
  // check their status before each scheduler tick
  bool wait_regist_result =
      k_scheduler_register_waiting_dependency(waiting_proc, waited_proc);
  if (!wait_regist_result) {
    return -1;  // Failed to register waiting dependency
  }
  terminal_logger_debug("k_proc_wait: waiting process %d, waited process %d\n",
                        waiting_proc->proc_id, waited_proc->proc_id);

  // Suspend the waiting process
  spthread_suspend_self();

  pcb_t* waited_proc_copy = k_scheduler_get_proc_by_pid(waited_proc_id);
  if (!waited_proc_copy) {
    terminal_logger_debug(
        "k_proc_wait: waited process %d not found, maybe terminated or "
        "quitted\n",
        waited_proc_id);
    return 0;
  }
  logger_log_life_cycle_event(k_scheduler_get_ticks(), "WAITED", waited_proc_id,
                              waited_proc->priority,
                              ((char**)waited_proc->argv)[0]);
  // When the waiting process is scheduled to run again, it will check the
  // status of the waited process. If it is a zombie, it will clean it up and
  // return the exit code. If it is not a zombie, it will just return -1.
  if (waited_proc->state == PROC_ZOMBIE) {
    // If the waited process is a zombie, we can just clean it up
    if (wstatus) {
      *wstatus = waited_proc->exit_code;
    }
    terminal_logger_debug(
        "k_proc_wait: waited process %d is a zombie, cleaning it up\n",
        waited_proc_id);
    k_scheduler_remove_pcb_by_pid(waited_proc->proc_id, waited_proc->priority);
    k_scheduler_remove_pcb_by_pid(waited_proc->proc_id, 3);
    k_proc_cleanup(waited_proc);
    return 0;
  } else if (waited_proc->state == PROC_STOPPED) {
    terminal_logger_debug(
        "k_proc_wait: waited process %d is stopped, resume the waiting "
        "process %d\n",
        waited_proc_id, waiting_proc_id);
    *wstatus = P_STOPPED_CODE;
    return 0;
  }
  return -1;
}

int k_proc_file_descriptor_table_init(pcb_t* pcb, pcb_t* parent_pcb) {
  if (pcb == NULL) {
    terminal_logger_debug(
        "k_proc_file_descriptor`_table_init: invalid PCB or already "
        "initialized\n");
    return -1;
  }

  if (pcb != NULL && pcb->proc_id != 1 && (vec_is_empty(&proc_fd_table))) {
    terminal_logger_debug(
        "k_proc_file_descriptor_table_init: invalid PCB or already "
        "initialized\n");
    return -1;
  }

  proc_table_entry_t* curr_fd_table_entry =
      (proc_table_entry_t*)malloc(sizeof(proc_table_entry_t));
  if (curr_fd_table_entry == NULL) {
    terminal_logger_debug(
        "k_proc_file_descriptor_table_init: could not allocate memory for "
        "file descriptor table\n");
    return -1;
  }
  curr_fd_table_entry->proc_id = pcb->proc_id;

  if (parent_pcb == NULL) {
    // Initialize the fd table with default values, only happens for init
    // process
    terminal_logger_debug(
        "k_proc_file_descriptor_table_init: parent PCB is NULL or has no "
        "file descriptor table for process %d\n",
        pcb->proc_id);
    for (int i = 0; i < MAX_FDS_PER_PROCESS; i++) {
      if (i < 3) {
        curr_fd_table_entry->fd_table.entries[i].file_index = -2;
      } else {
        curr_fd_table_entry->fd_table.entries[i].file_index = -1;
      }
      curr_fd_table_entry->fd_table.entries[i].cursor = 0;
    }
  } else {
    // Copy the fd table from the parent process
    int global_fd_table_size = vec_len(&proc_fd_table);
    for (int i = 0; i < global_fd_table_size; i++) {
      proc_table_entry_t* fd_entry =
          (proc_table_entry_t*)vec_get(&proc_fd_table, i);
      if (fd_entry != NULL && fd_entry->proc_id == parent_pcb->proc_id) {
        for (int j = 0; j < MAX_FDS_PER_PROCESS; j++) {
          curr_fd_table_entry->fd_table.entries[j].file_index =
              fd_entry->fd_table.entries[j].file_index;
          curr_fd_table_entry->fd_table.entries[j].cursor =
              fd_entry->fd_table.entries[j].cursor;
        }
      }
    }
  }

  vec_push_back(&proc_fd_table, curr_fd_table_entry);

  if (parent_pcb != NULL) {
    terminal_logger_debug(
        "k_proc_file_descriptor_table_init: copied file descriptor table from "
        "parent process %d to child process %d\n",
        parent_pcb->proc_id, pcb->proc_id);
  } else {
    terminal_logger_debug(
        "k_proc_file_descriptor_table_init: copied file descriptor table for "
        "init process %d\n",
        pcb->proc_id);
  }
  return 0;
}