#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "stdlib.h"
#include "system/pennfat.h"
#include "system/sys_calls.h"
#include "user/u_filesystemfunc.h"
#include "utils/parser.h"
#define FAT_ERROR 0xFFFF

extern scheduler_t* scheduler;

int main(int argc, char* argv[]) {
  // Ignore SIGINT and SIGTSTP so the shell is not interrupted.
  signal(SIGINT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);

  logger_set_debug_mode(false);
  logger_set_debug_file("./log/debug.log");

  k_scheduler_init();
  pcb_t* init_pcb = k_proc_create(NULL, NULL);
  k_proc_file_descriptor_table_init(init_pcb, NULL);
  scheduler->curr_proc = init_pcb;

  char line[1024];
  struct parsed_command* pcmd = NULL;
  const char* prompt = "pennfat> ";
  const char* exit_msg = "Exiting pennfat.\n";

  while (1) {
    write(STDERR_FILENO, prompt, strlen(prompt));
    if (s_fgets(line, sizeof(line), STDIN_FILENO) == NULL) {
      write(STDERR_FILENO, exit_msg, strlen(exit_msg));
      break;
    }
    // Remove trailing newline.
    line[strcspn(line, "\n")] = '\0';
    if (strlen(line) == 0)
      continue;

    // Use the parser to process the command line.
    int parse_ret = parse_command(line, &pcmd);
    if (parse_ret != 0) {
      print_parser_errcode(stderr, parse_ret);
      continue;
    }

    // The first command's arguments are in pcmd->commands[0]
    char** args = pcmd->commands[0];
    if (args[0] == NULL) {
      free(pcmd);
      continue;
    }
    if (strcmp(args[0], "mkfs") == 0) {
      // Expected: mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG
      if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
        const char* msg =
            "Usage: mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG\n";
        write(STDERR_FILENO, msg, strlen(msg));
      } else {
        s_mkfs(args[1], atoi(args[2]), atoi(args[3]));
      }
    } else if (strcmp(args[0], "mount") == 0) {
      // Expected: mount FS_NAME
      if (args[1] == NULL) {
        const char* msg = "Usage: mount FS_NAME\n";
        write(STDERR_FILENO, msg, strlen(msg));
      } else {
        s_mount(args[1]);
      }
    } else if (strcmp(args[0], "unmount") == 0) {
      s_unmount();
    } else if (strcmp(args[0], "ls") == 0) {
      s_ls();
    } else if (strcmp(args[0], "touch") == 0) {
      s_touch(args);
    } else if (strcmp(args[0], "cat") == 0) {
      s_cat(args);
    } else if (strcmp(args[0], "chmod") == 0) {
      s_chmod(args);
    } else if (strcmp(args[0], "mv") == 0) {
      s_mv(args);
    } else if (strcmp(args[0], "rm") == 0) {
      s_rm(args);
    } else if (strcmp(args[0], "cp") == 0) {
      s_cp(args);
    } else {
      const char* msg = "pennfat: command not found: ";
      write(STDERR_FD, msg, strlen(msg));
      write(STDERR_FD, args[0], strlen(args[0]));
      write(STDERR_FD, "\n", 1);
    }
    free(pcmd);
    pcmd = NULL;
    // print out proc_fd_table
  }
  return EXIT_SUCCESS;
}