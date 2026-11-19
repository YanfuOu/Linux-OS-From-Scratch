#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/k_file_system.h"
#include "../kernel/k_proc.h"       // for k_proc_create, k_proc_wait, etc.
#include "../kernel/k_scheduler.h"  // for k_scheduler_get_proc_by_pid, etc.
#include "../system/pennfat.h"
#include "../utils/utils.h"  // for join_argv_tail
#include "./sys_calls.h"  // a header where you declare s_spawn, s_waitpid, etc.
#include "s_io_marco.h"

#include <errno.h>

extern fat_st* current_fs;  // Global variable for the FAT filesystem

/******************************************************************************
 * s_spawn
 *
 * Create a child process that executes the function `func(arg)`. The child
 * inherits some attributes from its parent, and uses fd0/fd1 for I/O, etc.
 * Return the child's PID on success, or -1 on error.
 *****************************************************************************/
pid_t s_spawn(void* (*func)(void*), char* argv[], int input_fd, int output_fd) {
  // Get the parent (calling) process
  terminal_logger_debug("s_spawn: argv[0]: %s\n", argv[0]);
  pcb_t* parent = k_scheduler_get_current_proc();
  if (!parent || argv == NULL) {
    return -1;
  }
  int argc = 0;
  while (argv[argc] != NULL) {
    argc++;
  }

  struct parsed_command* pcmd = NULL;
  int parsed_result = parse_command(argv[0], &pcmd);
  if (parsed_result < 0) {
    // error: failed to parse command
    return -1;
  }
  // If the last non-NULL argv is &, remove it and set the pcb backgroud
  bool is_background = pcmd->is_background;

  // Create a child PCB (kernel-level)
  char** child_argv = string_to_argv(argv[0]);
  for (int i = 0; child_argv[i] != NULL; i++) {
    terminal_logger_debug("s_spawn: child_argv[%d]: %s\n", i, child_argv[i]);
  }
  pcb_t* child = k_proc_create(func, child_argv);
  if (!child) {
    // error: kernel-level creation failed
    return -1;
  }

  child->parent_proc_id = parent->proc_id;
  int init_result = k_proc_file_descriptor_table_init(child, parent);
  if (init_result < 0) {
    // error: failed to initialize file descriptor table
    free(child_argv);
    return -1;
  }
  if (input_fd != STDIN_FD) {
    int s_dup2_result = s_dup2(STDIN_FD, input_fd, child->proc_id,
                               argv[0]);  // return back input_fd
    // int s_dup2_result = s_dup2(input_fd, STDIN_FD);
    if (s_dup2_result < 0) {
      // error: failed to duplicate file descriptor
      terminal_logger_debug(
          "s_spawn: Failed to duplicate input file descriptor %d to %d\n",
          input_fd, STDIN_FD);
      free(child_argv);
      return -1;
    }
  }

  if (output_fd != STDOUT_FD) {
    int s_dup2_result = s_dup2(STDOUT_FD, output_fd, child->proc_id,
                               argv[0]);  // will return output_fd
    // int s_dup2_result = s_dup2(output_fd, STDOUT_FD);
    if (s_dup2_result < 0) {
      // error: failed to duplicate file descriptor
      terminal_logger_debug(
          "s_spawn: Failed to duplicate output file descriptor %d to %d\n",
          output_fd, STDOUT_FD);
      free(child_argv);
      return -1;
    }
  }
  vec_push_back(&(parent->children_proc_ids), (ptr_t)(intptr_t)child->proc_id);
  child->num_children = 0;  // Initially, no children
  child->function = func;
  child->state = PROC_SUSPENDED;
  child->priority = parent->priority;
  child->is_background_process = is_background;

  // Possibly set the child's priority to some default (1), or same as parent
  child->priority = parent->priority;
  k_scheduler_enqueue(child);
  if (!is_background) {
    scheduler_t* scheduler = k_get_scheduler();
    if (scheduler == NULL) {
      free(child_argv);
      return -1;  // Scheduler not initialized
    }
  }

  // Log a "CREATE" event:
  logger_log_life_cycle_event(k_get_scheduler()->tick_count, "CREATE",
                              child->proc_id, child->priority, argv[0]);

  // Return child's PID to parent
  return child->proc_id;
}

/******************************************************************************
 * s_waitpid
 *
 * Wait on a child of the calling process until it changes state (exit, stop,
 * etc.). If 'nohang' is true, do not block if no child is ready to report.
 * Return the child's PID on success, or -1 on error.
 *****************************************************************************/
pid_t s_waitpid(pid_t pid, int* wstatus, bool nohang) {
  // If pid == -1, wait for any child, or if pid > 0, wait for that child.
  pcb_t* parent = k_scheduler_get_current_proc();
  if (!parent) {
    // no such parent
    return -1;
  }
  if (pid == -1) {
    int len = vec_len(&parent->children_proc_ids);
    for (size_t i = 0; i < len; i++) {
      pid_t child_pid = (pid_t)(intptr_t)vec_get(&parent->children_proc_ids, i);
      pcb_t* child = k_scheduler_get_proc_by_pid(child_pid);
      if (!child) {
        continue;  // Skip if child no longer exists
      }

      int flags = nohang ? 1 : 0;
      // printf("%d Waiting for child %d\n", parent->proc_id, child->proc_id);
      int rc = k_proc_wait(parent, child, wstatus, flags);
      if (rc == 0) {
        return child_pid;
      }

      // If nohang is true and no child has changed state, continue
      if (nohang && i != len - 1) {
        continue;
      } else {
        // If nohang is false, we should be blocked in k_proc_wait
        // If we reach here, something went wrong
        terminal_logger_debug(
            "s_waitpid: nohang is false, but no child has changed state\n");
        return -1;
      }
    }

    // If we get here with nohang=true, no child has changed state
    return -1;
  }

  pcb_t* child = k_scheduler_get_proc_by_pid(pid);
  if (!child) {
    // no such child
    return -1;
  }
  pid_t child_pid = child->proc_id;

  // Call kernel-level wait, which may block parent if nohang==false.
  // Suppose k_proc_wait updates *wstatus with the child's exit code or
  // status code.
  int flags = nohang ? 1 : 0;
  // printf("%d Waiting for child %d\n", parent->proc_id, child->proc_id);
  int rc = k_proc_wait(parent, child, wstatus, flags);
  if (rc < 0) {
    // error
    return -1;
  }

  // If the child changed state, typically k_proc_wait returns 0 on success.
  // We can return the child's PID (or 0 if no child changed state and
  // if no child changed, return 0. Otherwise,
  // child->proc_id
  return child_pid;
}

/******************************************************************************
 * s_kill
 *
 * Send a PennOS signal (P_SIGSTOP, P_SIGCONT, P_SIGTERM, etc.) to the target
 * process. Return 0 on success, or -1 on error.
 *****************************************************************************/
int s_kill(pid_t sender_pid, pid_t target_pid, int signal_num) {
  if (target_pid <= 0 || target_pid <= 0 ||
      (signal_num != P_SIGSTOP && signal_num != P_SIGCONT &&
       signal_num != P_SIGTERM)) {
    return -1;  // invalid pid or signal
  }

  bool register_signal_result =
      k_scheduler_register_signal(sender_pid, target_pid, signal_num);

  if (!register_signal_result) {
    terminal_logger_debug(
        "s_kill: Failed to register signal %d from %d to %d\n", signal_num,
        sender_pid, target_pid);
    return -1;  // failed to register signal
  }
  terminal_logger_debug("s_kill: Registered signal %d from %d to %d\n",
                        signal_num, sender_pid, target_pid);
  return 0;  // success
}

/******************************************************************************
 * s_exit
 *
 * Unconditionally exit the calling process. No return.
 *****************************************************************************/
void s_exit(void) {
  pcb_t* self = k_scheduler_get_current_proc();
  if (!self) {
    // Exiting without a process
    exit(0);
  }
  // Log the exit event:
  // logger_log_life_cycle_event(k_get_scheduler()->tick_count, "EXIT",
  //                               self->proc_id, self->priority,
  //                               ((char**)self->argv)[0]);
  terminal_logger_debug("s_exit: Process %d exiting with code %d\n",
                        self->proc_id, P_EXITED_CODE);
  k_proc_exit(self, (void*)P_EXITED_CODE);
}

/******************************************************************************
 * s_nice
 *
 * Adjust the priority of the specified process. Return 0 on success, -1 on
 *error.
 *****************************************************************************/
int s_nice(pid_t pid, int priority) {
  if (priority < 0 || priority > 2) {
    return -1;  // invalid priority
  }
  pcb_t* target = k_scheduler_get_proc_by_pid(pid);
  if (!target) {
    return -1;
  }

  // Update the priority
  // int old = target->priority;
  target->priority = priority;

  // Log a "NICE" event:
  // k_logger_log_nice(get_system_ticks(), pid, old, priority, target->cmd);

  return EXIT_SUCCESS;
}

/******************************************************************************
 * s_sleep
 *
 * Suspend execution of the calling process for the specified number of clock
 * ticks. If a P_SIGTERM arrives, the process wakes early.
 *****************************************************************************/
void s_sleep(unsigned long ticks) {
  if (ticks == 0) {
    return;  // do nothing
  }
  pcb_t* self = k_scheduler_get_current_proc();
  if (!self) {
    // no current process
    const char* err_msg = "Sleeping Error: no current process\n";
    k_write(current_fs, STDERR_FILENO, err_msg, strlen(err_msg));
    return;
  }
  // Call the kernel-level sleep function
  k_proc_sleep(self, ticks);
}

pid_t s_getpid() {
  pcb_t* self = k_scheduler_get_current_proc();
  if (!self) {
    return -1;  // no current process
  }
  return self->proc_id;
}

int s_printf(int fd, const char* fmt, ...) {
  va_list ap;
  char stackbuf[10240];
  char* buf = stackbuf;
  size_t cap = sizeof stackbuf;
  int need;

  // first pass – size only
  va_start(ap, fmt);
  need = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);

  if (need < 0)
    return -1;  // formatting failed */

  // grow if the stack buffer is too small
  if ((size_t)need >= cap) {
    cap = (size_t)need + 1;
    buf = malloc(cap);
    if (!buf)
      return -1;

    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
  }

  // single PennOS syscall – NO host-OS I/O here
  int rc = s_write(fd, buf, need);

  if (buf != stackbuf)
    free(buf);
  return rc;  // bytes written or -1
}
