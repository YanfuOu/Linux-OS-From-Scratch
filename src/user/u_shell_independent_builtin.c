#include "u_shell_independent_builtin.h"
#include "../system/pennfat.h"
#include "../system/s_io_marco.h"
#include "../system/s_scheduler.h"
#include "../system/sys_calls.h"
#include "../utils/Vec.h"
#include "../utils/heap.h"
#include "u_job.h"

extern Vec bg_jobs;
extern Vec stopped_jobs;
extern Vec polled_jobs;
extern job_t* curr_fg_job;

void* u_sleep(void* arg) {
  char** args = (char**)arg;
  int arg_count = 0;
  while (args[arg_count] != NULL) {
    arg_count++;
  }
  if (arg_count < 2) {
    const char* err_msg = "Error: Invalid arguments for sleep\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }
  unsigned long seconds = atoi(args[1]);
  if (seconds <= 0) {
    const char* err_msg = "Error: Invalid seconds for sleep\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  unsigned long ticks = seconds * TICKS_PER_SEC;
  s_sleep(ticks);
  return NULL;
}

void* u_busy(void* arg) {
  while (true) {
  }
  return NULL;
}

void* u_ps(void* arg) {
  s_scheduler_ps();
  s_exit();
  return NULL;
}

void* u_kill(void* arg) {
  char** args = (char**)arg;
  int argc = 0;
  while (args[argc] != NULL) {
    argc++;
  }

  if (argc < 2) {
    const char* err_msg = "Error: Invalid usage of kill\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    s_exit();
    return NULL;
  }

  char* signal_name = args[1];
  int signal = P_SIGTERM;
  int start = 2;

  if (strcmp(signal_name, "-term") == 0) {
    signal = P_SIGTERM;
  } else if (strcmp(signal_name, "-stop") == 0) {
    signal = P_SIGSTOP;
  } else if (strcmp(signal_name, "-cont") == 0) {
    signal = P_SIGCONT;
  } else if (atoi(signal_name) != 0) {
    start = 1;
  } else {
    const char* err_msg = "Error: Invalid signal name\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    s_exit();
    return NULL;
  }

  pid_t curr_pid = s_getpid();

  for (int i = start; i < argc; i++) {
    int pid = atoi(args[i]);
    if (pid <= 2) {  // cannot kill 1 init and 2 shell
      const char* err_msg = "Error: Invalid PID\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      continue;
    }
    int result = s_kill(curr_pid, pid, signal);
    if (result == -1) {
      const char* err_msg = "Error: Failed to send signal\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    } else {
      const char* success_msg = "Signal sent successfully\n";
      s_write(STDERR_FILENO, success_msg, strlen(success_msg));
    }
  }
  s_exit();
  return NULL;
}

void* u_zombie_child(void* arg) {
  (void)arg;
  s_exit();
  return NULL;
}

void* u_zombify(void* arg) {
  (void)arg;
  // spawn zombie_child
  // s_spawn requires a function pointer plus optional argv, fd0, fd1
  char* argv[] = {"zombie_child", NULL};
  pid_t pid =
      s_spawn((void* (*)(void*))u_zombie_child, argv, STDIN_FD, STDOUT_FD);
  if (pid == -1) {
    // error spawning child
    return NULL;
  }
  // no call to s_waitpid => child remains zombie
  while (1) {
    // spin forever
    // the child is never waited on -> zombie
  }
  s_exit();
  // never reached
  return NULL;
}

void* u_orphan_child(void* arg) {
  (void)arg;
  // Please sir, I want some more
  while (1) {
    // child running
  }
  s_exit();  // never reached
  return NULL;
}

void* u_orphanify(void* arg) {
  (void)arg;
  // spawn orphan_child
  char* argv[] = {"orphan_child", NULL};
  pid_t pid =
      s_spawn((void* (*)(void*))u_orphan_child, argv, STDIN_FD, STDOUT_FD);
  if (pid == -1) {
    // error spawning child
    s_exit();
  }
  // parent returns => parent's process ends => child is orphan
  s_exit();
  // never reached
  return NULL;
}
