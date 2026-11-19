#ifndef PENNOS_U_JOB_H
#define PENNOS_U_JOB_H

#include <unistd.h>
#include "../utils/Vec.h"

typedef enum {
  JOB_CREATED,
  JOB_RUNNING,
  JOB_STOPPED,
  JOB_FINISHED,
  JOB_KILLED
} job_state_t;

typedef int job_id_t;

/**
 * @brief A structure defined for the job for the shell
 * @details This structure holds the necessary information for managing a job
 * including its state, process ID, command, priority, and exit code.
 */
typedef struct {
  /**
   * @brief The job ID of the job
   */
  job_id_t job_id;

  /**
   * @brief The state of the job
   */
  job_state_t state;

  /**
   * @brief The process ID of the job
   */
  pid_t pid;

  /**
   * @brief The command associated with the job
   */
  struct parsed_command* cmd;

  /**
   * @brief The priority of the job
   */
  int priority;

  /**
   * @brief The exit code of the job
   */
  int exit_code;
} job_t;

/**
 * @brief A comparator function defined for the job node in the job queue
 *
 * This comparator is defined to compare two jobs based on their job IDs. It is
 * used for sorting jobs in the queue into a min-heap and print out the jobs
 */
int job_comparator(ptr_t job_a_ptr, ptr_t job_b_ptr);

#endif  // PENNOS_U_JOB_H