#ifndef PENNOS_U_SHELL_BUILTIN_H
#define PENNOS_U_SHELL_BUILTIN_H

#include "../utils/Vec.h"
#include "../utils/heap.h"
#include "u_job.h"

/**
 * @brief Spawn a new process for `command` and set its priority to `priority`.
 * Then adjust the priority level of an existing process.
 *
 * Example Usage: nice 2 cat f1 f2 f3 (spawns cat with priority 2)
 */
void* u_nice(void* arg);

/**
 * @brief Adjust the priority level of an existing process.
 *
 * Example Usage: nice_pid 0 123 (sets priority 0 to PID 123)
 */
void* u_nice_pid(void* arg);

/**
 * @brief Lists all available commands.
 *
 * Example Usage: man
 */
void* u_man(void* arg);

/**
 * @brief Resumes the most recently stopped job in the background, or the job
 * specified by job_id.
 *
 * Example Usage: bg
 * Example Usage: bg 2 (job_id is 2)
 */
void* u_bg(void* arg);

/**
 * @brief Brings the most recently stopped or background job to the foreground,
 * or the job specified by job_id.
 *
 * Example Usage: fg
 * Example Usage: fg 2 (job_id is 2)
 */
void* u_fg(void* arg);

/**
 * @brief Lists all jobs.
 *
 * Example Usage: jobs
 */
void* u_jobs(void* arg);

/**
 * @brief Exits the shell and shutsdown PennOS.
 *
 * Example Usage: logout
 */
void* u_logout(void* arg);

#endif  // PENNOS_U_SHELL_BUILTIN_H