#ifndef PENNOS_U_SHELL_H
#define PENNOS_U_SHELL_H

#define CMD_COUNT 20

/**
 * @brief The shell process that runs the shell environment.
 *
 * Default called by the init process.
 */
void* u_shell(void* arg);

/**
 * @brief A handler for the Ctrl C interrupt.
 *
 * When Ctrl C is pressed, it registers a signal to be handled to the scheduler
 * with `P_SIGTERM`. The current front-desk running process would then be
 * temrinated in the next tick by the scheduler in the signal dependency
 * polling.
 *
 */
void u_shell_handler(int signum);

/**
 * @brief Polling all the jobs in the background job vector.
 *
 * It would check all the background running jobs, wait for each of them with
 * nohang option, and remove those stopped or finished jobs from the vector for
 * running background jobs.
 */
void u_shell_bg_polling();

/**
 * @brief Polling all the jobs in the foreground job vector.
 *
 * It would check all the foreground running jobs, wait for each of them with
 * nohang option, and remove those stopped or finished jobs from the vector for
 * running foreground jobs.
 */
void u_shell_stopped_polling();

/**
 * @brief Display all the jobs that have status updates.
 */
void u_shell_display_polled_jobs();

/**
 * @brief A command executor for the input command of the shell.
 *
 * It would take in the line input to the shell by the user, parsed it, than
 * dispatch a proper function for the input command from the pre-defined mapping
 * between the input command and corresponding function pointers. After that, it
 * would use s_spawn to create a process for the command.
 *
 * @param line The input command line from the user.
 * @param priority The priority of the command to be executed
 * @return void This function does not return anything.
 */
void u_shell_execute_command(char* line, int priority);

#endif  // PENNOS_U_SHELL_H