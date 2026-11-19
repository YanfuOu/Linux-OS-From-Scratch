#include "pennfat.h"
#include "stress.h"
#include "system/s_scheduler.h"  // for s_scheduler_init, etc.
#include "system/sys_calls.h"  // a header where you declare s_spawn, s_waitpid, etc.
#include "utils/utils.h"       // for readline

int main(int argc, char* argv[]) {
  // Depend on the input of the executable file, if it is launched with -v, then
  // enable debug output
  hang(argv);
}