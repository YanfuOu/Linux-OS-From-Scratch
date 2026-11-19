#ifndef PENNOS_LOGGER_H
#define PENNOS_LOGGER_H

#include <stdbool.h>
#include <stdio.h>

static FILE* logger_file = NULL;
static FILE* debug_logger_file = NULL;
static bool debug_model = false;
const static char* schedule_pattern = "[%lu]\t%s\t%d\t%d\t%s\n";
const static char* life_cycle_pattern = "[%lu]\t%s\t%d\t%d\t%s\n";
const static char* nice_patter = "[%lu]\tNICE\t%d\t%d\t%d\t%s\n";

/**
 * @brief Initialize the logger, determine whether to enable the debug mode
 *
 * This function initializes the logger by setting the debug mode. When the
 * debug mode is enabled, the debug output would be printed out to both the
 * terminal and the debug log file, and if is disabled, only the debug log file
 * will be used.
 *
 * @param debug: true to enable the debug mode, false to disable it
 */
void logger_set_debug_mode(bool debug);

/**
 * @brief Set the log file path
 *
 * @param path: The path to the log file
 */
void logger_set_file(const char* path);

/**
 * @brief Set the debug log file path
 *
 * @param path: The path to the debug log file
 */
void logger_set_debug_file(const char* path);

/**
 * @brief Log the schedule event
 *
 * @param ticks: The current tick of the scheduler
 * @param pid: The process id of the process
 * @param queue_num: The queue number of the process
 * @param cmd: The command of the process
 */
void logger_log_schedule(unsigned long ticks,
                         int pid,
                         int queue_num,
                         const char* cmd);

/**
 * @brief Log the life cycle event
 *
 * @param ticks: The current tick of the scheduler
 * @param event: The event of the process
 * @param pid: The process id of the process
 * @param nice_value: The nice value of the process
 * @param cmd: The command of the process
 */
void logger_log_life_cycle_event(unsigned long ticks,
                                 const char* event,
                                 int pid,
                                 int nice_value,
                                 const char* cmd);

/**
 * @brief Log the nice event
 *
 * @param ticks: The current tick of the scheduler
 * @param pid: The process id of the process
 * @param old_nice_value: The old nice value of the process
 * @param new_nice_value: The new nice value of the process
 * @param cmd: The command of the process
 */
void logger_log_nice_event(unsigned long ticks,
                           int pid,
                           int old_nice_value,
                           int new_nice_value,
                           const char* cmd);

/**
 * @brief Close the logger
 *
 * This function closes the logger and frees the resources.
 */
void logger_close();

/**
 * @brief Log the debug message
 *
 * @param format: The format of the debug message
 * @param ...: The arguments of the debug message
 */
void terminal_logger_debug(const char* format, ...);

#endif  // PENNOS_LOGGER_H
