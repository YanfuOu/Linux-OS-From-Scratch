

#include "u_filesystemfunc.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../system/sys_calls.h"
#include "../utils/utils.h"
#include "../system/pennfat.h"
#include "u_filesystemfunc.h"
/**
 * u_cat：user‐level cat
 * available for:
 *   cat file1 file2
 *   cat file1 file2 > out
 *   cat file1 file2 >> out
 *   cat < in
 *   cat < in > out
 *   cat
 */
void* u_cat(void* arg) {
  char** args = (char**)arg;

  // FIXME: Fix the spawn logics or the funciton here to pass
  // desired arguments to s_cat and chmod
  // 1) Retrieve the argument list from the parsed command
  int argc = 0;
  while (args[argc]) {
    terminal_logger_debug("u_cat: args[%d] = %s\n", argc, args[argc]);
    argc++;
  }

  char* argv_string = join_argv_tail(args, argc, 0);
  if (argv_string == NULL) {
    s_printf(STDERR_FILENO, "u_cat: Failed to join arguments\n");
    return NULL;
  }

  struct parsed_command* pcmd = NULL;
  int parsed_result = parse_command(argv_string, &pcmd);
  if (parsed_result < 0) {
    s_printf(STDERR_FILENO, "u_cat: Failed to parse command\n");
    free(argv_string);
    return NULL;
  }

  // 2) Reconstruct argv for s_cat: treat shell '<' redirection as input
  // filename, and translate '>'/'>>' to '-w'/'-a'
  char* new_argv[100];
  int na = 0;
  new_argv[na++] = args[0];  // "cat"

  // Preserve original file arguments
  for (int i = 1; pcmd->commands[0][i]; i++) {
    new_argv[na++] = pcmd->commands[0][i];
  }

  // Input redirection: append infile to input list
  if (pcmd->stdin_file) {
    new_argv[na++] = (char*)pcmd->stdin_file;
  }

  // Output redirection: '-w' for '>', '-a' for '>>'
  if (pcmd->stdout_file) {
    new_argv[na++] = pcmd->is_file_append ? "-a" : "-w";
    new_argv[na++] = (char*)pcmd->stdout_file;
  }
  new_argv[na] = NULL;
  // }

  // 3) Call s_cat to execute the concatenation logic
  s_cat(new_argv);
  s_exit();
  return NULL;
}

/**
 * @brief Echo back an input string.
 *
 * Example Usage: echo Hello World
 */
void* u_echo(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[0]) {
    s_printf(STDERR_FILENO, "u_echo: Invalid arguments\n");
    return NULL;
  }

  // Skip argv[0] ("echo") and print each remaining arg
  for (int i = 1; argv[i]; i++) {
    size_t len = strlen(argv[i]);
    // use your shell‐level write: write to FD=1 (stdout)
    s_write(STDOUT_FILENO, argv[i], (int)len);
    if (argv[i + 1]) {
      s_write(STDOUT_FILENO, " ", 1);
    }
  }
  // terminate with newline
  s_write(STDOUT_FILENO, "\n", 1);
  s_exit();
  return NULL;
}

void* u_ls(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[0]) {
    s_printf(STDERR_FILENO, "u_ls: Invalid arguments\n");
    return NULL;
  }

  if (argv[1]) {
    s_printf(STDERR_FILENO,
             "ls: path support not implemented yet (extra credit)\n");
  }

  s_ls();
  s_exit();
  return NULL;
}

/**
 * @brief For each file, create an empty file if it doesn't exist,
 *        else update its timestamp.
 *
 * Example Usage: touch f1 f2 f3 f4 f5
 */
void* u_touch(void* arg) {
  s_touch(arg);
  s_exit();
  return NULL;
}

/**
 * @brief Rename a file. If the dst_file already exists, overwrite it.
 *
 * Print an error if:
 *  - src_file does not exist
 *  - src_file lacks read permission
 *  - dst_file exists but lacks write permission
 *
 * Example Usage: mv src_file dst_file
 */
void* u_mv(void* arg) {
  // Delegate to the filesystem‐level s_mv implementation
  s_mv(arg);
  s_exit();
  return NULL;
}

/**
 * @brief Copy a file. If the dst_file already exists, overwrite it.
 *
 * Print an error if:
 *  - src_file does not exist
 *  - src_file lacks read permission
 *  - dst_file exists but lacks write permission
 *
 * Example Usage:
 *   cp src_file dst_file
 *   cp -h host_src dst_file
 *   cp src_file -h host_dst
 */
void* u_cp(void* arg) {
  s_cp(arg);
  s_exit();
  return NULL;
}

void* u_rm(void* arg) {
  // Delegate to filesystem‐level unlink logic
  s_rm(arg);
  s_exit();
  return NULL;
}
