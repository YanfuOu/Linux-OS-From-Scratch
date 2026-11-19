#ifndef PENNOS_SHELL_INDEPENDENT_BUILTIN_H
#define PENNOS_SHELL_INDEPENDENT_BUILTIN_H

#ifndef TICKS_PER_SEC
#define TICKS_PER_SEC 10
#endif

/**
 * @brief Sleep for `n` seconds.
 *
 * Note that you'll have to convert the number of seconds to the correct number
 * of ticks.
 *
 * Example Usage: sleep 10
 */
void* u_sleep(void* arg);

/**
 * @brief Busy wait indefinitely.
 * It can only be interrupted via signals.
 *
 * Example Usage: busy
 */
void* u_busy(void* arg);

/**
 * @brief List all processes on PennOS, displaying PID, PPID, priority, status,
 * and command name.
 *
 * Example Usage: ps
 */
void* u_ps(void* arg);

/**
 * @brief Sends a specified signal to a list of processes.
 * If a signal name is not specified, default to "term".
 * Valid signals are -term, -stop, and -cont.
 *
 * Example Usage: kill 1 2 3 (sends term to processes 1, 2, and 3)
 * Example Usage: kill -term 1 2 (sends term to processes 1 and 2)
 * Example Usage: kill -stop 1 2 (sends stop to processes 1 and 2)
 * Example Usage: kill -cont 1 (sends cont to process 1)
 */
void* u_kill(void* arg);

/**
 * @brief Helper for zombify.
 */
void* u_zombie_child(void* arg);

/**
 * @brief Used to test zombifying functionality of your kernel.
 *
 * Example Usage: zombify
 */
void* u_zombify(void* arg);

/**
 * @brief Helper for orphanify.
 */
void* u_orphan_child(void* arg);

/**
 * @brief Used to test orphanifying functionality of your kernel.
 *
 * Example Usage: orphanify
 */
void* u_orphanify(void* arg);

#endif  // PENNOS_SHELL_INDEPENDENT_BUILTIN_H