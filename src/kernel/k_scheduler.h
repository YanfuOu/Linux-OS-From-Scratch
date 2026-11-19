#ifndef K_SCHEDULER_H
#define K_SCHEDULER_H

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include "../utils/proc_queue.h"
#include "k_proc.h"
#include "utils/logger.h"

/*!
 * A structure for blocking waiting dependencies.
 * Each pair here stores two pointers to the waiting process(usually the parent
 * process) and the waited process(usually the child process).
 */
typedef struct sleeping_schduler_st {
  /**
   * @brief A pointer to the process that is sleeping
   */
  pcb_t* sleeping_proc;
  /**
   * @brief A pointer to the process that is being waited on
   */
  unsigned long remaining_ticks;
} sleeping_scheduler_t;

/**
 * A structure defined for the waiting relationship between processes.
 * Each pair of such dependency contains:
 *
 * 1. A pcb_t pointer of a waiting process, usually the parent process
 *
 * 2. A pcb_t pointer of a waited process, usually the child process
 */
typedef struct waiting_dependency_st {
  /**
   * @brief A pointer to the process that is waiting
   */
  pcb_t* waiting_proc;

  /**
   * @brief A pointer to the process that is being waited on
   */
  pcb_t* waited_proc;
} waiting_dependency_t;

/**
 * A structure defined for managing the signal of processes.aligned_alloc
 *
 * Each pair of such dependency contains:
 *
 * 1. A signal number, which belongs to the three PennOS-defined signals:
 * P_SIGTERM, P_SIGSTOP, P_SIGCONT
 *
 * 2. A pcb_t pointer of the sender process, usually the shell process or the
 * process called s_kill system call.
 *
 * 3. A pcb_t pointer of the receiver process, usually the process that is being
 * notified.
 */
typedef struct signal_dependency_st {
  /**
   * @brief The signal number of the signal
   */
  int signal_num;

  /**
   * @brief A pcb pointer of the process that sent the signal
   */
  pcb_t* sender;

  /**
   * @brief A pcb pointer of the process that is to receive the signal
   */
  pcb_t* receiver;
} signal_dependency_t;

// Scheduler structure
typedef struct scheduler_t {
  /**
   * @brief The scheduler queue, which contains all the processes
   *
   * 4 queues are defined for the scheduler, each queue is a linked list of the
   * `proc_ndoe_t` type. These 4 queues represent the 3 priority levels of the
   * processes and a queue for stopped, zombied, and terminated process.
   */
  proc_queue_t* queues[4];

  /**
   * @brief A pointer to the init process
   */
  pcb_t* init_proc;

  /**
   * @brief The current priority level of the scheduler
   *
   * The scheduler would run the process in the queue according to the order
   * in the `k_schedule_rounds` array, i.e., k_scheduler_rounds[index %
   * k_schedule_rounds_size].
   */
  int current_priority;

  /**
   * @brief The current process scheduled by the scheduler at the current tick
   */
  pcb_t* curr_proc;

  /**
   * @brief The current tick of the scheduler
   */
  unsigned long tick_count;

  /**
   * @brief A vector containing all the waiting dependencies
   */
  Vec waiting_dependency_vec;

  /**
   * @brief A vector containing all the signal dependencies
   */
  Vec signal_dependency_vec;

  /**
   * @brief A vector containing all the sleeping processes
   */
  Vec sleeping_vec;
} scheduler_t;

// A fixed array of scheduler orders, which is used to determine which
// priority queue to run next. Each time, the scheduler would run the
// process in the queue according to the order in this array, i.e.,
// k_scheduler_rounds[index % k_schedule_rounds_size].
static const int k_schedule_rounds[] = {0, 1, 0, 2, 0, 1, 0, 2, 0, 1,
                                        0, 2, 0, 1, 0, 2, 0, 1, 1};

static const int k_schedule_rounds_size =
    sizeof(k_schedule_rounds) / sizeof(int);

static const int centisecond = 10000;

/**
 * @brief Init the scheduler for PennOS.
 *
 * The init process includes:
 *
 * 1. Allocating the memory for the scheduler structure
 *
 * 2. Init all the internal sdata structures, including the queues and
 * dependency vectors.
 */
void k_scheduler_init();

/*!
 * @brief Scheduler tick function.
 * This function is called when the timer interrupt occurs.
 * It first set the signal mask for the scheduler, and then it would do the
 * following:
 * 0. Polling the signal dependencies, waiting dependencies, all the orphan
 *    child processes, and sleeping the sleeping time of sleeping processes.
 * 1. Get the next priority of processes to run, this is determined
 *    by the k_schedule_rounds array
 * 2. Dequeue the next process from the scheduler queue, and checks
 *    if the process is still valid, if so, continue it
 * 3. If the process is not valid, it would simply enqueue it to the end of the
 *    third queue(0-indexed) of the scheduler
 * 4. Setting the signal mask of the scheduler, waiting for the selected process
 *    to run
 * 5. Check if the current process is still running, if so,
 *    suspend it, and enqueue it to the scheduler queue based on
 *    its priority. If not, simply enqueue it to the third queue
 * 6. Increment the tick count of the scheduler
 */
void k_scheduler_tick();

/*!
 * @breif Put a process into the correct priority queue so that the scheduler
 * can find it. The queue to be put into is determined by:
 * 1. The priority of the process
 * 2. The state of the process
 *
 * If the process is in the state of `PROC_SUSPENDED`, it will be put into the
 * queue corresponding to its priority. If the process is all other states, it
 * will be put into the third queue.
 *
 * @param proc: the process to be put into the queue
 */
void k_scheduler_enqueue(pcb_t* proc);

/*!
 * @brief Dequeue a process from the scheduler queue based on the parameter. It
 * would only pop the process from the beginning of the queue.
 *
 * @param priority_level: the priority level of the process to be dequeued
 * @return pcb_t*: the process that is dequeued from the queue, NULL if failed
 */
pcb_t* k_scheduler_dequeue(int priority_level);

/*!
 * @breif Dequeue a process from the scheduler queue based on the parameter. It
 * would only pop the process from the beginning of the queue.
 *
 * @param priority_level: the priority level of the process to be dequeued
 * @return `pcb_t*`: the process that is dequeued from the queue, NULL if failed
 */
pcb_t* k_scheduler_get_proc_by_pid(pid_t pid);

/*!
 * @breif Get the current process of the scheduler
 * @return `pcb_t*`: the current process of the scheduler, NULL if failed
 */
pcb_t* k_scheduler_get_current_proc();

/*!
 * @breif Remove a process from the scheduler queue based on the parameter and
 * the pid of the process to be removed.
 *
 * @param pid: the pid of the process to be removed
 * @param priority_level: the priority level of the process to be removed
 * @return `pcb_t*`: the process that is removed from the queue, NULL if failed
 */
pcb_t* k_scheduler_remove_pcb_by_pid(pid_t pid, int priority_level);

/*!
 * @breif  A Wrapper of the `k_scheduler_tick` function
 */
void k_schedule(void);

/*
 * @breif A placeholder for the signal handler for SIGALRM
 */
void k_alarm_handler(int signum);

/*!
 * @breif Get the scheduler structure
 * @return `scheduler_t*`: the scheduler structure, NULL if failed
 */
scheduler_t* k_get_scheduler();

/*!
 * @breif Create a waiting dependency between two processes.
 * The waiting process is the one that is waiting for the waited process to
 * finish. The waited process is the one that is being waited on.
 *
 * This dependency would be put into the waiting dependency vector of the
 * scheduler.
 *
 * @param waiting_proc: the process that is waiting
 * @param waited_proc: the process that is being waited on
 * @return `bool`: true if success, false if failed
 */
bool k_scheduler_register_waiting_dependency(pcb_t* waiting_proc,
                                             pcb_t* waited_proc);

/*!
 * @breif Unregister a waiting dependency between two processes.
 * The waiting process is the one that is waiting for the waited process to
 * finish. The waited process is the one that is being waited on.
 *
 * This dependency would be removed from the waiting dependency vector of the
 * scheduler.
 *
 * @param waiting_proc: the process that is waiting
 * @param waited_proc: the process that is being waited on
 * @return `bool`: true if success, false if failed
 */
bool k_scheduler_unregister_waiting_dependency(pcb_t* waiting_proc,
                                               pcb_t* waited_proc);

/*!
 * @breif Polling the waiting dependencies, which including checking the status
 * of the waited processes and changing the queue of the waiting processes
 * depending on the waited result of the waited process.
 *
 * When the waited process is in the state of `PROC_ZOMBIE`, the waiting process
 * would be unblocked, i.e., its state would be changed to `PROC_SUSPENDED`,
 * then it would be removed from the third queue and put into the queue
 * corresponding to its priority level.
 *
 * This function will check all waiting dependencies and update the states
 * of the waiting processes accordingly.
 *
 * @return `void`: This function does not return a value.
 */
void k_scheduler_polling_waiting_dependency();

/*!
 * @breif Poll the signal dependencies and handle any processes that have been
 * signaled.
 *
 * This function iterates through the signal dependency vector in
 * reverse order, processes each dependency, and removes it from the vector.
 *
 * For each dependency:
 * - Logs the signal reception and lifecycle event for the process.
 * - Terminates the process associated with the signal.
 * - Removes the dependency from the signal dependency vector.
 *
 * This ensures that all signaled processes are handled and cleaned up properly.
 *
 * @return `void`: This function does not return a value.
 */
void k_scheduler_polling_signal_dependency();

/*!
 * @breif A helper function to print the scheduler queue for debugging purposes.
 */
void k_scheduler_print_queue();

/*!
 * @breif Get the initial process of the scheduler.
 * @return `pcb_t*`: the initial process of the scheduler, NULL if failed
 */
pcb_t* k_scheduler_get_init();

/*!
 * @breif Set the initial process of the scheduler.
 * @param init_proc: the initial process of the scheduler
 */
void k_scheduler_set_init(pcb_t* init_proc);

/*!
 * @breif Get a process from the queue based on the pid.
 * @param queue: the queue to get the process from
 * @param pid: the pid of the process to get
 * @return `pcb_t*`: the process that is got from the queue, NULL if failed
 */
pcb_t* k_scheduler_get_pcb_from_queue(proc_queue_t* queue, pid_t pid);

/*!
 * @breif Reap orphan processes by having the init process wait for its zombie
 * children.
 *
 *  This function iterates through the list of child process IDs of the
 * init process and attempts to wait for each child process. If the child
 * process is a zombie, it is reaped and removed from the init process's list of
 * children.
 *
 * Steps:
 *
 * - Retrieve the init process control block (PCB).
 *
 * - If the init process is NULL, return immediately.
 *
 * - Iterate through the list of child process IDs of the init process in
 * reverse order:
 *    1. Skip invalid child process IDs (-1).
 *    2. Retrieve the PCB of the child process.
 *    3. Attempt to wait for the child process using `k_proc_wait`.
 *    4. If the wait is successful, remove the child process ID from the list.
 *    5. If the wait fails, log the error (commented out for now).
 *
 * This function ensures that orphaned zombie processes are cleaned up by the
 * init process.
 *
 * @return `void`: This function does not return a value.
 */
void k_scheduler_reap_orphan_processes();

/*!
 * @breif Get the current system ticks.
 * @return `unsigned long`: the current system ticks
 */
long k_scheduler_get_ticks();
/**
 * @breif Polling the sleeping process each tick.
 *
 * The logic of the sleeping polling is as follows:
 *
 * 1. Iterate through the sleeping vector in the scheduler.
 *
 * 2. For each sleeping process, we check its state, if it is not blocked, which
 * means it has been stopped by a signal, we skip it. When the state of the
 * process is still blocked and the reamining ticks is not 0, we decrement the
 * remaining ticks by 1. If the remaining ticks is 0, we change the state of the
 * process to `PROC_SUSPENDED`, and enqueue it to the queue corresponding to its
 * priority level, and this would make the process a zombie process since it has
 * finished.
 */
void k_scheduler_polling_sleeping_time();

/*!
 * @breif Handle the SIGINT signal (Ctrl-C) by sending a termination signal
 * (P_SIGTERM) to the front process in the scheduler. If no front process
 * exists, the program exits without cleaning up processes.
 *
 * Steps:
 *
 * - Logs the reception of the Ctrl-C signal.
 *
 * - Logs the process ID of the front process.
 *
 * - Allocates a new signal dependency structure.
 *
 * - Sets the sender to shell, i.e., process with pid 2, the receiver to the
 * current process, and the signal number to P_SIGTERM.
 *
 * - Pushes the signal dependency into the scheduler's signal dependency
 * vector.
 *
 * @param pid: The target process's proc_id
 * @return `bool`: The result of the signal registration.
 */
bool k_scheduler_register_signal(pid_t sender_pid,
                                 pid_t target_pid,
                                 int signal_num);

/**
 * @breif Clean up the scheduler by freeing all the resources used by the
 * scheduler.
 */
void k_scheduler_clean_up();

#endif  // K_SCHEDULER_H
