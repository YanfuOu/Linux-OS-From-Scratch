#include "u_shell_builtin.h"
#include "../kernel/k_proc.h"
#include "../system/pennfat.h"
#include "../system/sys_calls.h"
#include "../utils/Vec.h"
#include "../utils/utils.h"
#include "stdio.h"
#include "u_shell.h"
#include "u_shell_command.h"

extern Vec bg_jobs;
extern Vec stopped_jobs;
extern Vec polled_jobs;
extern job_t* curr_fg_job;
extern shell_command_t shell_commands[];
extern job_id_t job_id_counter;

job_t* get_job_by_job_id(job_id_t job_id) {
  int bg_jobs_size = vec_len(&bg_jobs);
  for (int i = 0; i < bg_jobs_size; i++) {
    job_t* job = (job_t*)vec_get(&bg_jobs, i);
    if (job->job_id == job_id) {
      return job;
    }
  }
  int stopped_jobs_size = vec_len(&stopped_jobs);
  for (int i = 0; i < stopped_jobs_size; i++) {
    job_t* job = (job_t*)vec_get(&stopped_jobs, i);
    if (job->job_id == job_id) {
      return job;
    }
  }
  return NULL;
}

job_t* get_most_recent_stopped_or_bg_job() {
  int stopped_jobs_size = vec_len(&stopped_jobs);
  if (stopped_jobs_size > 0) {
    return vec_get(&stopped_jobs, stopped_jobs_size - 1);
  }

  int bg_jobs_size = vec_len(&bg_jobs);
  if (bg_jobs_size > 0) {
    job_t* job_with_max_id = NULL;
    for (int i = 0; i < bg_jobs_size; i++) {
      job_t* job = (job_t*)vec_get(&bg_jobs, i);
      if (job_with_max_id == NULL || job->job_id > job_with_max_id->job_id) {
        job_with_max_id = job;
      }
    }
    return job_with_max_id;
  }
  return NULL;
}

void* u_nice(void* arg) {
  char** args = (char**)arg;
  int argc = 0;
  while (args[argc] != NULL) {
    argc++;
  }

  if (argc < 2) {
    const char* err_msg = "Error: Insufficient arguments for nice\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  int priority = atoi(args[1]);
  if (priority < 0 || priority > 2) {
    const char* err_msg = "Error: Invalid priority level\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  int start_idx = 2;
  char* line = join_argv_tail(args, argc, start_idx);
  u_shell_execute_command(line, priority);
  return NULL;
}

void* u_nice_pid(void* arg) {
  char** args = (char**)arg;
  if (args == NULL || args[0] == NULL || strcmp(args[0], "nice_pid") != 0) {
    const char* err_msg = "Error: Invalid or NULL command for nice_pid\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  int arg_count = 0;
  while (args[arg_count] != NULL) {
    arg_count++;
  }

  if (arg_count < 3) {
    const char* err_msg = "Error: Insufficient arguments for nice_pid\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  int pid = atoi(args[2]);
  int priority = atoi(args[1]);

  if (pid <= 0) {
    const char* err_msg = "Error: Invalid PID\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  if (priority < 0 || priority > 2) {
    const char* err_msg = "Error: Invalid priority level\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  int result = s_nice(pid, priority);
  if (result == -1) {
    const char* err_msg = "Error: Failed to change priority\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
  } else {
    const char* success_msg = "Success: Priority changed\n";
    s_write(STDERR_FILENO, success_msg, strlen(success_msg));
  }
  return NULL;
}

void* u_man(void* arg) {
  for (int i = 0; i < CMD_COUNT; i++) {
    const char* command_name = shell_commands[i].command_name;
    if (command_name != NULL) {
      // fprintf(stderr, "%s\n", command_name);
      s_printf(STDERR_FILENO, "%s\n", command_name);
    }
  }
  return NULL;
}

void* u_bg(void* arg) {
  char** args = (char**)arg;
  if (args == NULL || args[0] == NULL || strcmp(args[0], "bg") != 0) {
    const char* err_msg = "Error: Invalid or NULL command for bg\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  job_id_t job_id = args[1] != NULL ? atoi(args[1]) : -1;
  terminal_logger_debug("u_bg: job_id = %d\n", job_id);
  job_t* job = NULL;

  if (job_id == -1) {
    job = get_most_recent_stopped_or_bg_job();
    if (job == NULL || job->state == JOB_RUNNING ||
        job->state == JOB_FINISHED) {
      const char* err_msg = "Error: No stopped background job found\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    }
  } else {
    job = get_job_by_job_id(job_id);
    if (job == NULL) {
      const char* err_msg =
          "Error: No stopped or background job found with specified ID\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    } else if (job->state == JOB_RUNNING || job->state == JOB_FINISHED) {
      const char* err_msg = "Error: Job is already running or finished\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    }
  }

  // Using the s_kill function to send SIGCONT signal to the job
  int continue_result = s_kill(2, job->pid, P_SIGCONT);
  if (continue_result == -1) {
    const char* err_msg = "Error: Failed to continue job\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }
  job->state = JOB_RUNNING;
  terminal_logger_debug("Job %d continued\n", job->job_id);
  // Remove the job from stopped_jobs and add it to bg_jobs
  int index = -1;
  int stopped_jobs_size = vec_len(&stopped_jobs);
  for (int i = 0; i < stopped_jobs_size; i++) {
    job_t* stopped_job = (job_t*)vec_get(&stopped_jobs, i);
    if (stopped_job->job_id == job->job_id) {
      index = i;
      break;
    }
  }
  if (index != -1) {
    vec_erase(&stopped_jobs, index);
    terminal_logger_debug("Job %d moved from stopped to background\n",
                          job->job_id);
  } else {
    terminal_logger_debug("Job %d not found in stopped jobs\n", job->job_id);
  }
  vec_push_back(&bg_jobs, job);
  // fprintf(stderr, "[%d] %d\n", job->job_id, job->pid);
  s_printf(STDERR_FILENO, "[%d] %d\n", job->job_id, job->pid);
  return NULL;
}

void* u_fg(void* arg) {
  char** args = (char**)arg;
  if (args == NULL || args[0] == NULL || strcmp(args[0], "fg") != 0) {
    const char* err_msg = "Error: Invalid or NULL command for fg\n";
    s_write(STDERR_FILENO, err_msg, strlen(err_msg));
    return NULL;
  }

  job_id_t job_id = args[1] != NULL ? atoi(args[1]) : -1;
  terminal_logger_debug("u_fg: job_id = %d\n", job_id);
  job_t* job = NULL;
  if (job_id == -1) {
    job = get_most_recent_stopped_or_bg_job();
    if (job == NULL || job->state == JOB_FINISHED) {
      const char* err_msg = "Error: No stopped or background job found\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    }
  } else {
    job = get_job_by_job_id(job_id);
    if (job == NULL) {
      const char* err_msg =
          "Error: No stopped or background job found with specified ID\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    } else if (job->state == JOB_FINISHED) {
      const char* err_msg = "Error: Job is already finished\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    }
  }

  // For running jobs, simply bring it to the front and then wait it
  if (job->state == JOB_RUNNING) {
    int index = -1;
    int bg_jobs_size = vec_len(&bg_jobs);
    for (int i = 0; i < bg_jobs_size; i++) {
      job_t* bg_job = (job_t*)vec_get(&bg_jobs, i);
      if (bg_job->job_id == job->job_id) {
        index = i;
        break;
      }
    }
    if (index != -1) {
      vec_erase(&bg_jobs, index);
      terminal_logger_debug("Job %d moved from background to foreground\n",
                            job->job_id);
    } else {
      terminal_logger_debug("Job %d not found in background jobs\n",
                            job->job_id);
    }

  } else {
    // For stopped jobs, send SIGCONT to continue it,
    // and then bring it back to the front, wait it
    int continue_result = s_kill(2, job->pid, P_SIGCONT);
    if (continue_result == -1) {
      const char* err_msg = "Error: Failed to continue job\n";
      s_write(STDERR_FILENO, err_msg, strlen(err_msg));
      return NULL;
    }
    job->state = JOB_RUNNING;
    terminal_logger_debug("Job %d continued\n", job->job_id);
    // Remove the job from stopped_jobs and add it to bg_jobs
    int index = -1;
    int stopped_jobs_size = vec_len(&stopped_jobs);
    for (int i = 0; i < stopped_jobs_size; i++) {
      job_t* stopped_job = (job_t*)vec_get(&stopped_jobs, i);
      if (stopped_job->job_id == job->job_id) {
        index = i;
        break;
      }
    }
    if (index != -1) {
      vec_erase(&stopped_jobs, index);
      terminal_logger_debug("Job %d moved from stopped to background\n",
                            job->job_id);
    } else {
      terminal_logger_debug("Job %d not found in stopped jobs\n", job->job_id);
    }
  }
  if (job != NULL) {
    curr_fg_job = job;
  }
  return NULL;
}

void* u_jobs(void* arg) {
  size_t job_count = vec_len(&bg_jobs) + vec_len(&stopped_jobs);
  Heap jobs = heap_new(job_count, job_comparator);
  for (int i = 0; i < vec_len(&bg_jobs); i++) {
    job_t* job = (job_t*)vec_get(&bg_jobs, i);
    heap_push(&jobs, job);
  }
  for (int i = 0; i < vec_len(&stopped_jobs); i++) {
    job_t* job = (job_t*)vec_get(&stopped_jobs, i);
    heap_push(&jobs, job);
  }
  for (int i = 0; i < job_count; i++) {
    job_t* job = (job_t*)heap_pop(&jobs);
    if (job->state == JOB_RUNNING) {
      // fprintf(stderr, "[%d] %d Running\n", job->job_id, job->pid);
      s_printf(STDERR_FILENO, "[%d] %d Running\n", job->job_id, job->pid);
    } else if (job->state == JOB_STOPPED) {
      s_printf(STDERR_FILENO, "[%d] %d Stopped\n", job->job_id, job->pid);
    }
  }
  heap_destroy(&jobs);
  return NULL;
}

void* u_logout(void* arg) {
  s_exit();
  return NULL;
}