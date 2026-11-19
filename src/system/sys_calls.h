#ifndef PENNOS_SYS_CALLS_H
#define PENNOS_SYS_CALLS_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Create a child process that executes the function `func`.
 * The child will retain some attributes of the parent.
 *
 * @param func Function to be executed by the child process.
 * @param argv Null-terminated array of args, including the command name as
 * argv[0].
 * @param input_fd File descriptor for the child's standard input.
 * @param output_fd File descriptor for the child's standard output.
 * @return pid_t The process ID of the created child process.
 */
// pid_t s_spawn(void* (*func)(void*), char* argv[]);
pid_t s_spawn(void* (*func)(void*), char* argv[], int input_fd, int output_fd);

/**
 * @brief Wait on a child of the calling process, until it changes state.
 * If `nohang` is true, this will not block the calling process and return
 * immediately.
 *
 * @param pid Process ID of the child to wait for.
 * @param wstatus Pointer to an integer variable where the status will be
 * stored.
 * @param nohang If true, return immediately if no child has exited.
 * @return pid_t The process ID of the child which has changed state on success,
 * -1 on error, returns 1 if the waited child is stopped due to P_SIGSTOP.
 */
pid_t s_waitpid(pid_t pid, int* wstatus, bool nohang);

/**
 * @brief Send a signal to a particular process.
 *
 * @param pid Process ID of the target proces.
 * @param signal Signal number to be sent.
 * @return 0 on success, -1 on error.
 */
int s_kill(pid_t sender_pid, pid_t target_pid, int signal_num);

/**
 * @brief Unconditionally exit the calling process.
 */
void s_exit(void);

/**
 * @brief Set the priority of the specified thread.
 *
 * @param pid Process ID of the target thread.
 * @param priority The new priorty value of the thread (0, 1, or 2)
 * @return 0 on success, -1 on failure.
 */
int s_nice(pid_t pid, int priority);

/**
 * @brief Suspends execution of the calling proces for a specified number of
 * clock ticks.
 *
 * This function is analogous to `sleep(3)` in Linux, with the behavior that the
 * system clock continues to tick even if the call is interrupted. The sleep can
 * be interrupted by a P_SIGTERM signal, after which the function will return
 * prematurely.
 *
 * @param ticks Duration of the sleep in system clock ticks. Must be greater
 * than 0.
 */
void s_sleep(unsigned long ticks);

/**
 * @brief Get the process proc_id of the calling process.
 *
 * @return pid_t The process ID of the calling process.
 */
pid_t s_getpid();

/**
 * @brief A formatted output function that writes to a specified file
 * descriptor.
 *
 * This function takes a file descriptor, a format string, and a variable number
 * of arguments to format the output. It is similar to the standard fprintf
 * function.
 *
 * @param fd The file descriptor to write to.
 * @param str The format string.
 * @param ... The variable arguments to format the output.
 * @return int The number of characters written, or -1 on error.
 */
int s_printf(int fd, const char* str, ...);

#endif  // PENNOS_SYS_CALLS_H