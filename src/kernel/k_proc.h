#ifndef K_PROC_H
#define K_PROC_H

#include <stdint.h>
#include "../spthread/spthread.h"
#include "../utils/Vec.h"
#include "../utils/panic.h"
#include "../utils/parser.h"
#include "../utils/utils.h"
#include "k_file_system.h"
#include "utils/logger.h"

typedef int signal_t;

// Process states
enum proc_state {
  PROC_RUNNING,
  PROC_SUSPENDED,
  PROC_BLOCKED,
  PROC_STOPPED,
  PROC_ZOMBIE,
  PROC_INVALID
};

// Several macros for the signals used in PennOS
#define P_SIGSTOP 1
#define P_SIGCONT 2
#define P_SIGTERM 3

// Process exit codes
enum {
  P_EXITED_CODE = 0,    // child exited normally
  P_STOPPED_CODE = 1,   // child was stopped by a signal
  P_SIGNALED_CODE = 2,  // child was killed by a signal
  P_BLOCKED_CODE = 3,   // child was blocked by a signal
  P_RUNNING_CODE = 4,   // child is running
};

// Extract the low nibble from wstatus:
#define P_CODE(wstatus) ((int)(wstatus) & 0xF)

// Return true if the child terminated normally
#define P_WIFEXITED(wstatus) (P_CODE(wstatus) == P_EXITED_CODE)

// Return true if the child was stopped by a signal
#define P_WIFSTOPPED(wstatus) (P_CODE(wstatus) == P_STOPPED_CODE)

// Return true if the child was terminated by a signal (SIGTERM)
#define P_WIFSIGNALED(wstatus) (P_CODE(wstatus) == P_SIGNALED_CODE)

// Return true if the child was blocked
#define P_WIFBLOCKED(wstatus) (P_CODE(wstatus) == P_BLOCKED_CODE)

// Return true if the child is running (SIGCONT)
#define P_WIFRUNNING(wstatus) (P_CODE(wstatus) == P_RUNNING_CODE)

/**
 * A structure contains all the essential meta data about a thread.
 */
typedef struct pcb_t {
  /**
   * @brief The process id of the process
   */
  pid_t proc_id;

  /**
   * @brief The process id of the parent process
   */
  pid_t parent_proc_id;

  /**
   * @brief A vector containing all the children processes' pids
   */
  Vec children_proc_ids;

  /**
   * @brief The number of children processes
   */
  int num_children;

  /**
   * @brief A pointer to the function to be executed by the process
   */
  void* (*function)(void*);

  /**
   * @brief A pointer to the argument to be passed to the function
   */
  void* argv;

  /**
   * @brief A pointer to the return value of the function
   */
  void* return_value;

  /**
   * @brief The file descriptor table of the process
   */
  enum proc_state state;

  /**
   * @brief The actual spthread_t thread of the process
   */
  spthread_t* thread;

  /**
   * @brief The signal mask of the process
   */
  int priority;

  /**
   * @brief A flag indicating if the process is a background process, not used
   * for now
   */
  bool is_background_process;

  /**
   * @brief Exiting status of the process
   */
  int exit_status;

  /**
   * @brief Exiting code of the process
   */
  int exit_code;
} pcb_t;

/**
 * Create a new process control block
 * @param function: the function to be executed by the process
 * @param arg: the argument to be passed to the function
 * @return a pointer to the new process control block
 */
pcb_t* k_proc_create(void* (*function)(void*), void* arg);

/*!
 * Suspend the process no matter it is running or not
 * @param pcb: the process control block
 * @return:
 * - 0 on success
 * - EAGAIN if the thread could not be signaled
 * - ENOSYS if not supported on this system
 * - ESRCH if the thread specified is not a valid pthread
 */
int k_proc_suspend(pcb_t* pcb);

/*!
 * Continue the process no matter it is running or not
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @return 0 if success, -1 if failed
 */
int k_proc_continue(pcb_t* pcb);

/*!
 * Cancel the process no matter it is running or not
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @return 0 if success, -1 if failed
 */
int k_proc_cancel(pcb_t* pcb);

/*!
 * Exit the process, recoding its exit status
 *
 * When a process exits using s_exit, which would call this function, all its
 * non-zombified chidlren processes would be migrated under the init process.
 *
 * @param pcb: the process control block
 * @param status: the status of the process when exiting
 */
void k_proc_exit(pcb_t* pcb, void* status);

/*!
 * Terminate the process no matter it is running or not
 * When a process is terminated, all its children processes would be migrated
 * under the init process.
 *
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @return 0 if success, -1 if failed
 */
int k_proc_sigterm(pcb_t* pcb);

/*!
 * Stop the process no matter it is running or not
 * Stop a process already stopped would not change its state
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @return 0 if success, -1 if failed
 */
int k_proc_sigstop(pcb_t* pcb);

/*!
 * Continue the process no matter it is running or not
 * Continue a process already running would not change its state
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @return 0 if success, -1 if failed
 */
int k_proc_sigcont(pcb_t* pcb);

/*!
 * Sleep the process for the given seconds
 * Return 0 if success, -1 if failed
 * @param pcb: the process control block
 * @param ticks: number of clock ticks to sleep
 * @return 0 if success, -1 if failed
 */
int k_proc_sleep(pcb_t* pcb, unsigned int ticks);

/*!
 * Cleanup the process, releasing the resources
 * @param pcb: the process control block
 */
void k_proc_cleanup(pcb_t* pcb);

/*!
 * Get the process id of the process
 * @param pcb: the process control block
 * @return the process id of the process
 */
pid_t k_proc_get_pid(pcb_t* pcb);

/*!
 * Sending a signal to the process
 * Return 0 if success, -1 if failed
 * @param signal: the signal to be sent
 * @param pcb: the process control block of the target process
 * @return 0 if success, -1 if failed
 */
int k_proc_kill(signal_t signal, pcb_t* pcb);

/*!
 * Waiting for the process to exit
 *
 * This function would block the calling process until the waited process exits
 * if the flags is set to 0. Return 0 if success, -1 if failed
 *
 * When the nohang option is used, i.e., flags == true, the function would
 * directly return after checking the state of the waited process. The side
 * eeffects of the non-blocking wait include:
 *
 * 1. If the waited process is a zombie, all its children process would be
 * migrate under the children list of the init process without changing the
 * children processes' states.
 *
 * 2. If the waited process is a zombie, after migrating its children processes,
 * the waited process's resource would be cleaned using `k_proc_cleanup`.
 *
 * When the blocking option is used, i.e. flags == false, the function would:
 *
 * 1. Check the state of the waited process, if it is a zombie, then clean it up
 * and return
 *
 * 2. If the waited process is not a zombie, this kernel function would create a
 * waiting dependency pair between the waiting process and the waited process,
 * and such a dependency would be registered to the scheduler's waiting
 * dependency list. This dependency would be checked at the beginning of each
 * tick of the scheduler using the `k_scheduler_polling_waiting_dependency`
 * function.
 *
 * 3. After registering the waiting dependency, the waiting process would put
 * itself into a blocked state using the `spthread_suspend_self` function until
 * the scheduler wakes it up.
 *
 * 4. When the scheduler wakes it up, which means there is a state change of the
 * waited process, the waiting process would check the state of the waited
 * process, if it is a zombie, then save its exiting status, clean it up and
 * return.
 *
 * @param waiting_proc: the process control block of the waiting process
 * @param waited_proc: the process control block of the waited process
 * @param wstatus: the status of the waited process
 * @param flags: the flags for waiting, 0 for blocking, 1 for non-blocking
 * @return 0 if success, -1 if failed, 1 when the waited process is stopped
 */
int k_proc_wait(pcb_t* waiting_proc,
                pcb_t* waited_proc,
                int* wstatus,
                int flags);

/*!
 * Init the file descriptor table for the speifcied process.
 * This process includes:
 *
 * 1. Malloc a new file descirptor table for the process;
 *
 * 2. Using memcpy to copy the file descriptor table of the parent process to
 * the new file descriptor table of the child process.
 *
 * @param pcb: the process control block of the process to be initialized
 * @return 0 if success, -1 if failed
 */
int k_proc_file_descriptor_table_init(pcb_t* pcb, pcb_t* parent_pcb);

#endif  // K_PROC_H
