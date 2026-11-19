#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/system/pennfat.h"
#include "system/s_scheduler.h"
#include "utils/logger.h"
#include "utils/parser.h"

int main(int argc, char* argv[]) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);

  char* fs_file = NULL;
  char* log_file = NULL;
  bool debug_eabled = false;

  if (argc == 1) {
    fprintf(stderr, "Usage: %s <fs_file> <log_file>\n", argv[0]);
    return 1;
  }

  if (argc == 2) {
    fs_file = argv[1];
    log_file = "log";
  } else if (argc == 3) {
    fs_file = argv[1];
    log_file = argv[2];
  } else if (argc == 4) {
    fs_file = argv[1];
    log_file = argv[2];
    debug_eabled = (strcmp(argv[3], "-v") == 0);
  }
  char log_file_path[256];
  snprintf(log_file_path, sizeof(log_file_path), "./log/%s", log_file);
  logger_set_debug_file("./log/debug.log");
  logger_set_file(log_file_path);
  logger_set_debug_mode(debug_eabled);
  if (debug_eabled) {
    terminal_logger_debug("Debug mode enabled\n");
  }
  int mounting_result = s_mount(fs_file);
  if (mounting_result == -1) {
    terminal_logger_debug("Error mounting filesystem %s\n", fs_file);
    return -1;
  }
  s_scheduler_init(argv);
  return 0;
}