#include "../src/system/s_scheduler.h"  // for s_scheduler_init, etc.
#include "../src/system/sys_calls.h"  // a header where you declare s_spawn, s_waitpid, etc.
#include "../src/utils/utils.h"  // for readline
#include "pennfat.h"

int main(int argc, char* argv[]) {
  // Depend on the input of the executable file, if it is launched with -v, then
  // enable debug output
  logger_set_debug_file("./log/debug.log");
  if (argc > 1 && strcmp(argv[1], "-v") == 0) {
    logger_set_debug_mode(true);
  }
  s_mount("minfs");
  s_scheduler_init(argv);
}