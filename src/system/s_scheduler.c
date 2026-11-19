#include "s_scheduler.h"
#include <stdbool.h>
#include "pennfat.h"
#include "../user/u_shell.h"
#include "../utils/heap.h"
#include "s_io_marco.h"
#include "sys_calls.h"

void s_scheduler_init(void* arg) {
  terminal_logger_debug("Initializing scheduler...\n");
  k_scheduler_init();

  char* args = ((char**)arg)[1];
  char* argv[] = {"init", args, NULL};
  pcb_t* init_proc = k_proc_create(s_init_func, argv);
  if (!init_proc) {
    // Error creating init process
    terminal_logger_debug("Failed to create init process\n");
    return;
  }
  k_proc_file_descriptor_table_init(init_proc, NULL);
  logger_log_life_cycle_event(k_get_scheduler()->tick_count, "CREATE",
                              init_proc->proc_id, init_proc->priority, "init");

  init_proc->parent_proc_id = 0;
  init_proc->priority = 0;  // Set default priority
  init_proc->state = PROC_SUSPENDED;
  init_proc->function = s_init_func;

  init_proc->argv = argv;
  k_scheduler_enqueue(init_proc);
  terminal_logger_debug("Scheduler initialized.\n");
  k_schedule();
  logger_close();
  s_exit();
}

void* s_init_func(void* args) {
  terminal_logger_debug("Init process started.\n");
  pcb_t* init_pcb = k_scheduler_get_current_proc();
  if (!init_pcb) {
    // Error: no current process
    return NULL;
  }
  k_scheduler_set_init(init_pcb);

  char* fs_name = ((char**)args)[1];
  // Init a shell environment
  char* shell_argv[] = {"shell", fs_name, NULL};
  pid_t shell_pid =
      s_spawn((void* (*)(void*))u_shell, shell_argv, STDIN_FD, STDOUT_FD);

  if (shell_pid == -1) {
    // Error creating shell process
    return NULL;
  }
  while (true) {
    int wstatus;
    int shell_wait = s_waitpid(shell_pid, &wstatus, false);
    if (shell_wait == -1) {
      terminal_logger_debug("Init process finished.\n");
      s_exit();
      return NULL;
    }
  }
  terminal_logger_debug("Init process finished.\n");
  s_exit();
  // This should never be reached
  return NULL;
}

char print_state(enum proc_state state) {
  switch (state) {
    case PROC_RUNNING:
      return 'R';
    case PROC_SUSPENDED:
      return 'R';
    case PROC_BLOCKED:
      return 'B';
    case PROC_STOPPED:
      return 'S';
    case PROC_ZOMBIE:
      return 'Z';
    default:
      return 'U';
  }
}

int proc_comparator(const ptr_t a, const ptr_t b) {
  if (a == NULL || b == NULL) {
    return 0;
  }
  pcb_t* proc_a = (pcb_t*)a;
  pcb_t* proc_b = (pcb_t*)b;

  if (proc_a->proc_id < proc_b->proc_id) {
    return -1;
  } else if (proc_a->proc_id > proc_b->proc_id) {
    return 1;
  } else {
    return 0;
  }
}

void s_scheduler_ps() {
  scheduler_t* scheduler = k_get_scheduler();
  if (scheduler == NULL) {
    return;
  }
  size_t proc_count = scheduler->queues[0]->size + scheduler->queues[1]->size +
                      scheduler->queues[2]->size + scheduler->queues[3]->size;
  if (scheduler->curr_proc != NULL) {
    proc_count++;
  }
  Heap proc_heap = heap_new(proc_count, proc_comparator);
  if (scheduler->curr_proc != NULL) {
    heap_push(&proc_heap, scheduler->curr_proc);
    terminal_logger_debug("Current process: %d with state %d\n",
                          scheduler->curr_proc->proc_id,
                          scheduler->curr_proc->state);
  }

  for (int i = 0; i < 4; i++) {
    proc_node_t* node = front(scheduler->queues[i]);
    while (node != NULL) {
      heap_push(&proc_heap, node->proc);
      node = node->next;
    }
  }

  // fprintf(stderr, "PID\tPPID\tPRI\tSTATE\tCMD\n");
  // while (!heap_is_empty(&proc_heap)) {
  //   pcb_t* proc = (pcb_t*)heap_pop(&proc_heap);
  //   fprintf(stderr, "%d\t%d\t%d\t%c\t%s\n", proc->proc_id,
  //   proc->parent_proc_id,
  //           proc->priority, print_state(proc->state),
  //           ((char**)proc->argv)[0]);
  // }

  const char* header = "PID\tPPID\tPRI\tSTATE\tCMD\n";
  s_write(STDOUT_FD, header, strlen(header));
  while (!heap_is_empty(&proc_heap)) {
    pcb_t* proc = (pcb_t*)heap_pop(&proc_heap);
    s_printf(STDOUT_FD, "%d\t%d\t%d\t%c\t%s\n", proc->proc_id,
             proc->parent_proc_id, proc->priority, print_state(proc->state),
             ((char**)proc->argv)[0]);
  }
  heap_destroy(&proc_heap);
}