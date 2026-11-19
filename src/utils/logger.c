#include "logger.h"
#include <errno.h>

void logger_set_debug_mode(bool debug) {
  debug_model = debug;
}

void logger_set_file(const char* path) {
  if (path == NULL) {
    return;
  }
  if (remove(path) != 0 && errno != ENOENT) {
    perror("Failed to clear log file");
    return;
  }
  logger_file = fopen(path, "a+");
  if (logger_file == NULL) {
    perror("Failed to open log file");
    return;
  }
  return;
}

void logger_set_debug_file(const char* path) {
  if (path == NULL) {
    return;
  }
  if (remove(path) != 0 && errno != ENOENT) {
    perror("Failed to clear debug log file");
    return;
  }
  debug_logger_file = fopen(path, "a+");
  if (debug_logger_file == NULL) {
    fprintf(stderr, "Failed to open debug log file %s\n", path);
    return;
  }
  return;
}

void logger_log_schedule(unsigned long ticks,
                         int pid,
                         int queue_num,
                         const char* cmd) {
  if (logger_file == NULL) {
    return;
  }

  fprintf(logger_file, schedule_pattern, ticks, cmd, pid, queue_num);
  fflush(logger_file);
}

void logger_log_life_cycle_event(unsigned long ticks,
                                 const char* event,
                                 int pid,
                                 int nice_value,
                                 const char* cmd) {
  if (logger_file == NULL) {
    return;
  }

  fprintf(logger_file, life_cycle_pattern, ticks, event, pid, nice_value, cmd);
  fflush(logger_file);
}

void logger_log_nice_event(unsigned long ticks,
                           int pid,
                           int old_nice_value,
                           int new_nice_value,
                           const char* cmd) {
  if (logger_file == NULL) {
    return;
  }

  fprintf(logger_file, nice_patter, ticks, pid, old_nice_value, new_nice_value,
          cmd);
  fflush(logger_file);
}

void logger_close() {
  if (logger_file != NULL) {
    fclose(logger_file);
    logger_file = NULL;
  }
}

void terminal_logger_debug(const char* format, ...) {
  va_list args;
  va_start(args, format);
  if (debug_model) {
    vfprintf(stderr, format, args);
    fflush(stderr);
  }
  vfprintf(debug_logger_file, format, args);
  fflush(debug_logger_file);
  va_end(args);
}