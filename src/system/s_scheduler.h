#ifndef S_SCHEDULER_H
#define S_SCHEDULER_H

#include "../kernel/k_proc.h"
#include "../kernel/k_scheduler.h"
#include "../utils/parser.h"
#include "../utils/utils.h"

/*!
 * @breif Create the scheduler in the system level.
 *
 * The entire initialization process includes:
 * - Create the scheduler
 * - Create the init process
 * - Run the init process with the scheduler
 * - Start scheduling
 */
void s_scheduler_init(void* arg);

/*!
 * @breif The actual function that runs the init process, it actually does
 * nothing, but launch a shell environment
 * @param args: the arguments to be passed to the function
 * @return: the return value of the function, which should be NULL
 */
void* s_init_func(void* arg);

/**
 * @breif The ps function as the system level, managed through access the
 * kernel-level scheduler information.
 *
 * It iterates throught all the processes managed by the current scheduler in
 * the kernel level, pushing all of them into a min-heap ordered by the
 * process's proc_id, and then print out the information of each process by
 * popping them out of the heap one by one.
 */
void s_scheduler_ps();

#endif  // S_SCHEDULER_H