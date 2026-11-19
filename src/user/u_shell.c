#include "u_shell.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../kernel/k_proc.h"
#include "../system/pennfat.h"
#include "../system/s_io_marco.h"
#include "../system/s_scheduler.h"
#include "../system/sys_calls.h"
#include "../utils/Vec.h"
#include "../utils/logger.h"
#include "../utils/parser.h"
#include "../utils/utils.h"
#include "stress.h"
#include "u_filesystemfunc.h"
#include "u_job.h"
#include "u_shell_builtin.h"
#include "u_shell_command.h"
#include "u_shell_independent_builtin.h"

Vec bg_jobs;
Vec stopped_jobs;
Vec polled_jobs;
job_t* curr_fg_job;
job_t* curr_job;
static job_id_t job_id_counter = 1;

#ifndef PENNOS_SHELL_COMMAND_TYPE_BUILTIN
#define SHELL_COMMAND_TYPE_BUILTIN 0
#define SHELL_COMMAND_TYPE_EXTERNAL 1
#define PENNOS_SHELL_PROMPT "$ "
#endif

shell_command_t shell_commands[] = {
    {"ps", u_ps, SHELL_COMMAND_TYPE_EXTERNAL},
    {"zombify", u_zombify, SHELL_COMMAND_TYPE_EXTERNAL},
    {"orphanify", u_orphanify, SHELL_COMMAND_TYPE_EXTERNAL},
    {"busy", u_busy, SHELL_COMMAND_TYPE_EXTERNAL},
    {"sleep", u_sleep, SHELL_COMMAND_TYPE_EXTERNAL},
    {"cat", u_cat, SHELL_COMMAND_TYPE_EXTERNAL},
    {"echo", u_echo, SHELL_COMMAND_TYPE_EXTERNAL},
    {"ls", u_ls, SHELL_COMMAND_TYPE_EXTERNAL},
    {"touch", u_touch, SHELL_COMMAND_TYPE_EXTERNAL},
    {"chmod", u_chmod, SHELL_COMMAND_TYPE_EXTERNAL},
    {"kill", u_kill, SHELL_COMMAND_TYPE_EXTERNAL},
    {"mv", u_mv, SHELL_COMMAND_TYPE_EXTERNAL},
    {"cp", u_cp, SHELL_COMMAND_TYPE_EXTERNAL},
    {"rm", u_rm, SHELL_COMMAND_TYPE_EXTERNAL},
    {"hang", hang, SHELL_COMMAND_TYPE_EXTERNAL},
    {"nohang", nohang, SHELL_COMMAND_TYPE_EXTERNAL},
    {"recur", recur, SHELL_COMMAND_TYPE_EXTERNAL},

    {"nice", u_nice, SHELL_COMMAND_TYPE_BUILTIN},
    {"nice_pid", u_nice_pid, SHELL_COMMAND_TYPE_BUILTIN},
    {"man", u_man, SHELL_COMMAND_TYPE_BUILTIN},
    {"fg", u_fg, SHELL_COMMAND_TYPE_BUILTIN},
    {"bg", u_bg, SHELL_COMMAND_TYPE_BUILTIN},
    {"jobs", u_jobs, SHELL_COMMAND_TYPE_BUILTIN},
    {"logout", u_logout, SHELL_COMMAND_TYPE_BUILTIN},
};

const static size_t command_num =
    sizeof(shell_commands) / sizeof(shell_command_t);

void* u_shell(void* arg) {
  bg_jobs = vec_new(0, NULL);
  stopped_jobs = vec_new(0, NULL);
  polled_jobs = vec_new(0, NULL);
  curr_fg_job = NULL;

  struct sigaction sa;
  sa.sa_handler = u_shell_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    panic("k_scheduler_init: sigaction failed");
  }
  if (sigaction(SIGTSTP, &sa, NULL) == -1) {
    perror("sigaction");
    panic("k_scheduler_init: sigaction failed");
  }

  while (true) {
    char line[1024];
    const char* prompt = PENNOS_SHELL_PROMPT;
    s_write(STDERR_FILENO, prompt, strlen(prompt));
    // ssize_t bytes_read = readline(&line);
    s_fgets(line, sizeof(line), STDIN_FILENO);
    // Remove trailing newline.
    line[strcspn(line, "\n")] = '\0';
    if (strlen(line) == 0) {
      continue;
    }

    // s_fgets(line, 1024, STDIN_FD);
    u_shell_bg_polling();
    u_shell_stopped_polling();

    int priority = 1;
    u_shell_execute_command(line, priority);
    u_shell_display_polled_jobs();
  }
}

void u_shell_handler(int signum) {
  if (curr_fg_job == NULL) {
    return;
  }
  terminal_logger_debug(
      "Shell: Ctrl C/Z signal received, registering with process %d\n",
      curr_fg_job->pid);
  pid_t curr_proc_pid = curr_fg_job->pid;
  signum = (signum == SIGINT ? P_SIGTERM : P_SIGSTOP);
  int registration_result = s_kill(2, curr_proc_pid, signum);
  if (registration_result == 0) {
    if (signum == P_SIGTERM) {
      terminal_logger_debug(
          "Shell: Ctrl C signal has been successfully registered with "
          "process "
          "%d\n",
          curr_proc_pid);
      curr_fg_job->state = JOB_FINISHED;
      vec_push_back(&polled_jobs, curr_fg_job);
      terminal_logger_debug("Job %d terminated\n", curr_fg_job->job_id);
      curr_fg_job = NULL;
    } else if (signum == P_SIGSTOP) {
      terminal_logger_debug(
          "Shell: Ctrl Z signal has been successfully registered with "
          "process "
          "%d\n",
          curr_proc_pid);
      curr_fg_job->state = JOB_STOPPED;
      vec_push_back(&stopped_jobs, curr_fg_job);
      terminal_logger_debug("Job %d stopped\n", curr_fg_job->job_id);
      curr_fg_job = NULL;
      const char* err_msg = " Stopped\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    }
  } else {
    if (signum == P_SIGTERM) {
      terminal_logger_debug(
          "Shell: Ctrl C signal registration failed for process %d\n",
          curr_proc_pid);
    } else if (signum == P_SIGSTOP) {
      terminal_logger_debug(
          "Shell: Ctrl Z signal registration failed for process %d\n",
          curr_proc_pid);
    }
  }
  return;
}

void u_shell_bg_polling() {
  if (vec_len(&bg_jobs) == 0) {
    terminal_logger_debug("No background jobs to poll\n");
    return;
  }

  terminal_logger_debug("Polling background jobs\n");

  for (int i = vec_len(&bg_jobs) - 1; i >= 0; i--) {
    job_t* bg_job = (job_t*)vec_get(&bg_jobs, i);
    terminal_logger_debug("Polling background job %d\n", bg_job->pid);
    int wstatus = -1;
    pid_t waited_pid = bg_job->pid;
    int waiting_result = s_waitpid(bg_job->pid, &wstatus, true);
    if (waiting_result == -1) {
      terminal_logger_debug("Waiting Result %d with Waiting Status: %d\n",
                            waiting_result, wstatus);
      if (P_WIFSTOPPED(wstatus)) {
        bg_job->state = JOB_STOPPED;
        terminal_logger_debug("Background job %d is stopped\n", bg_job->pid);
        vec_push_back(&stopped_jobs, bg_job);
        vec_push_back(&polled_jobs, bg_job);
        terminal_logger_debug("Job %d moved to stopped jobs\n", bg_job->job_id);
        vec_erase(&bg_jobs, i);
        terminal_logger_debug("Job %d removed from background jobs\n",
                              bg_job->job_id);
      } else if (P_WIFBLOCKED(wstatus)) {
        terminal_logger_debug(
            "Background job %d is still blocked, i.e., still running\n",
            bg_job->pid);
      } else if (P_WIFRUNNING(wstatus)) {
        terminal_logger_debug(
            "Error polling background job, still running %d\n", bg_job->pid);
      } else {
        terminal_logger_debug(
            "Error polling background job %d, removing jobs\n", bg_job->pid);
        bg_job->state = JOB_KILLED;
        terminal_logger_debug("Background job %d finished with status %d\n",
                              bg_job->pid, P_CODE(wstatus));
        vec_push_back(&polled_jobs, bg_job);
        terminal_logger_debug(
            "Job %d terminated and pushed back to polled vec\n",
            bg_job->job_id);
        vec_erase(&bg_jobs, i);
        terminal_logger_debug("Job %d removed from background jobs\n",
                              bg_job->job_id);
      }
      continue;
    } else if (waiting_result == waited_pid) {
      vec_push_back(&polled_jobs, bg_job);
      if (P_WIFEXITED(wstatus)) {
        bg_job->state = JOB_FINISHED;
        terminal_logger_debug("Background job %d exited with status %d\n",
                              bg_job->pid, P_CODE(wstatus));
        vec_erase(&bg_jobs, i);
        terminal_logger_debug("Job %d removed from background jobs\n",
                              bg_job->job_id);
      } else if (P_WIFSTOPPED(wstatus)) {
        bg_job->state = JOB_STOPPED;
        char* err_msg = " Stopped\n";
        s_write(STDERR_FILENO, err_msg, strlen(err_msg));
        terminal_logger_debug("Background job %d stopped with status %d\n",
                              bg_job->pid, P_CODE(wstatus));
        vec_push_back(&stopped_jobs, bg_job);
        terminal_logger_debug("Job %d moved to stopped jobs\n", bg_job->job_id);
        vec_erase(&bg_jobs, i);
      } else if (P_WIFSIGNALED(wstatus)) {
        bg_job->state = JOB_FINISHED;
        char* err_msg = " Killed by signal\n";
        s_write(STDERR_FILENO, err_msg, strlen(err_msg));
        terminal_logger_debug("Background job %d killed by signal %d\n",
                              bg_job->pid, P_CODE(wstatus));
        vec_push_back(&polled_jobs, bg_job);
        terminal_logger_debug(
            "Job %d terminated and pushed back to polled vec\n",
            bg_job->job_id);
        vec_erase(&bg_jobs, i);
        terminal_logger_debug("Job %d removed from background jobs\n",
                              bg_job->job_id);
      }
    } else {
      terminal_logger_debug("Unexpected result from s_waitpid: %d\n",
                            waiting_result);
    }
  }
}

void u_shell_stopped_polling() {
  if (vec_len(&stopped_jobs) == 0) {
    return;
  }

  for (int i = vec_len(&stopped_jobs) - 1; i >= 0; i--) {
    job_t* stopped_job = (job_t*)vec_get(&stopped_jobs, i);
    pid_t stopped_job_pid = stopped_job->pid;
    terminal_logger_debug("Polling stopped job %d\n", stopped_job_pid);
    int wstatus = -1;
    int waiting_result = s_waitpid(stopped_job_pid, &wstatus, true);
    if (waiting_result == -1) {
      terminal_logger_debug("Waiting Result %d with Waiting Status: %d\n",
                            waiting_result, wstatus);
      if (P_WIFRUNNING(wstatus)) {
        stopped_job->state = JOB_RUNNING;
        terminal_logger_debug("Stopped job %d is still running\n",
                              stopped_job_pid);
        vec_push_back(&bg_jobs, stopped_job);
        terminal_logger_debug("Job %d moved to background jobs\n",
                              stopped_job->job_id);
        vec_erase(&stopped_jobs, i);
        terminal_logger_debug("Job %d removed from stopped jobs\n",
                              stopped_job->job_id);
      } else if (P_WIFBLOCKED(wstatus)) {
        stopped_job->state = JOB_RUNNING;
        terminal_logger_debug("Stopped job %d is still blocked\n",
                              stopped_job_pid);
        terminal_logger_debug("Job %d is still running\n", stopped_job->job_id);
        vec_push_back(&bg_jobs, stopped_job);
        terminal_logger_debug("Job %d moved to background jobs\n",
                              stopped_job->job_id);
        vec_erase(&stopped_jobs, i);
        terminal_logger_debug("Job %d removed from stopped jobs\n",
                              stopped_job->job_id);
      } else if (P_WIFSTOPPED(wstatus)) {
        stopped_job->state = JOB_STOPPED;
        terminal_logger_debug("Stopped job %d is still stopped\n",
                              stopped_job_pid);
        terminal_logger_debug("Job %d is still running\n", stopped_job->job_id);
      } else if (P_WIFEXITED(wstatus)) {
        stopped_job->state = JOB_FINISHED;
        terminal_logger_debug("Stopped job %d exited with status %d\n",
                              stopped_job_pid, P_CODE(wstatus));
        vec_erase(&stopped_jobs, i);
        terminal_logger_debug("Job %d removed from stopped jobs\n",
                              stopped_job->job_id);
        vec_push_back(&polled_jobs, stopped_job);
      } else if (P_WIFSIGNALED(wstatus)) {
        stopped_job->state = JOB_KILLED;
        terminal_logger_debug("Stopped job %d killed by signal %d\n",
                              stopped_job_pid, P_CODE(wstatus));
        vec_erase(&stopped_jobs, i);
        vec_push_back(&polled_jobs, stopped_job);
        terminal_logger_debug("Job %d removed from stopped jobs\n",
                              stopped_job->job_id);
      }
    }
  }
  terminal_logger_debug("All stopped jobs polled\n");
}

void u_shell_display_polled_jobs() {
  if (vec_len(&polled_jobs) == 0) {
    return;
  }

  for (int i = vec_len(&polled_jobs) - 1; i >= 0; i--) {
    job_t* polled_job = (job_t*)vec_get(&polled_jobs, i);
    if (polled_job->state == JOB_FINISHED) {
      // fprintf(stderr, "[%d] %d Done\n", polled_job->job_id, polled_job->pid);
      s_printf(STDERR_FILENO, "[%d] %d Done\n", polled_job->job_id,
               polled_job->pid);
    } else if (polled_job->state == JOB_KILLED) {
      // fprintf(stderr, "[%d] %d Killed\n", polled_job->job_id,
      // polled_job->pid);
      s_printf(STDERR_FILENO, "[%d] %d Killed\n", polled_job->job_id,
               polled_job->pid);
    } else if (polled_job->state == JOB_STOPPED) {
      // fprintf(stderr, "[%d] %d Stopped\n", polled_job->job_id,
      // polled_job->pid);
      s_printf(STDERR_FILENO, "[%d] %d Stopped\n", polled_job->job_id,
               polled_job->pid);
    }
    vec_erase(&polled_jobs, i);
    terminal_logger_debug("Job %d removed from polled jobs\n",
                          polled_job->job_id);
  }
  terminal_logger_debug("All polled jobs displayed\n");
}

void u_shell_execute_command(char* line, int priority) {
  struct parsed_command* cmd = NULL;
  int parsing_result = parse_command(line, &cmd);
  if (parsing_result != 0) {
    terminal_logger_debug("Error parsing command: %d\n", parsing_result);
    print_parser_errcode(stderr, parsing_result);
    // free(line);
    return;
  }

  if (cmd->commands[0][0] == NULL) {
    // free(line);
    return;
  }

  shell_command_t input_command = {0};
  for (size_t i = 0; i < command_num; i++) {
    if (strcmp(cmd->commands[0][0], shell_commands[i].command_name) == 0) {
      input_command = shell_commands[i];
      break;
    }
  }

  // Handle the potential I/O redirection
  int input_fd = STDIN_FD;
  if (cmd->stdin_file != NULL) {
    input_fd = s_open(cmd->stdin_file, F_READ);
    if (input_fd < 0) {
      const char* err_msg = "Error: Failed to open input file\n";
      s_write(STDERR_FD, err_msg, strlen(err_msg));
      // free(line);
      return;
    }
  }

  int output_fd = STDOUT_FD;
  if (cmd->stdout_file != NULL) {
    if (cmd->is_file_append) {
      output_fd = s_open(cmd->stdout_file, F_APPEND);
    } else {
      output_fd = s_open(cmd->stdout_file, F_WRITE);
    }
    if (output_fd < 0) {
      char* touch_args[] = {"touch", (char*)cmd->stdout_file, NULL};
      s_touch(touch_args);
      output_fd = s_open(cmd->stdout_file, F_WRITE);
    }
  }

  if (input_command.command_name == NULL) {
    const char* err_msg = "Error: Command not found\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    // free(line);
    return;
  }

  for (size_t i = 0; cmd->commands[0][i] != NULL; i++) {
    terminal_logger_debug("Command %zu: %s\n", i, cmd->commands[0][i]);
  }

  if (input_command.command_type == SHELL_COMMAND_TYPE_EXTERNAL) {
    job_t* new_job = (job_t*)malloc(sizeof(job_t));
    new_job->job_id = job_id_counter++;
    new_job->state = JOB_CREATED;
    new_job->cmd = cmd;
    new_job->priority = priority;
    new_job->exit_code = 0;
    char** argv = wrap_single_string(line);
    new_job->pid = s_spawn((void* (*)(void*))input_command.command_func, argv,
                           input_fd, output_fd);
    s_nice(new_job->pid, priority);

    if (new_job->pid == -1) {
      terminal_logger_debug("Error creating process for command: %s\n",
                            cmd->commands[0][0]);
      free(new_job);
      // free(line);
      return;
    }

    new_job->state = JOB_RUNNING;
    terminal_logger_debug("Job %d running with PID %d\n", new_job->job_id,
                          new_job->pid);
    if (cmd->is_background) {
      // fprintf(stderr, "[%d] %d\n", new_job->job_id, new_job->pid);
      s_printf(STDERR_FILENO, "[%d] %d\n", new_job->job_id, new_job->pid);
      vec_push_back(&bg_jobs, new_job);
      return;
    } else {
      curr_fg_job = new_job;
    }
    curr_job = new_job;
  } else {
    // Run the builtin command by directly calling the function without
    // mallocing new jobs or s_spawn
    if (input_command.command_func != NULL) {
      input_command.command_func(cmd->commands[0]);
      free(cmd);
      // return;
    }
  }

  if (curr_fg_job != NULL) {
    int wstatus = 0;
    terminal_logger_debug("Waiting for process %d\n", curr_fg_job->pid);
    int waiting_result = s_waitpid(curr_fg_job->pid, &wstatus, false);
    if (waiting_result == -1) {
      terminal_logger_debug("Error waiting for process %d\n", curr_fg_job->pid);
      free(curr_fg_job);
      free(cmd);
      // free(line);
      return;
    } else if (waiting_result == 0) {
      if (P_WIFEXITED(wstatus)) {
        terminal_logger_debug("Process %d exited with status %d\n",
                              curr_fg_job->pid, P_CODE(wstatus));
      } else if (P_WIFSTOPPED(wstatus)) {
        terminal_logger_debug("Process %d stopped with status %d\n",
                              curr_fg_job->pid, P_CODE(wstatus));
      } else if (P_WIFSIGNALED(wstatus)) {
        terminal_logger_debug("Process %d killed by signal %d\n",
                              curr_fg_job->pid, P_CODE(wstatus));
      }
    }
    curr_fg_job = NULL;
  }
}