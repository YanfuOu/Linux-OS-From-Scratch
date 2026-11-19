
#include "pennfat.h"
#include "system/sys_calls.h"
#include "user/u_job.h"
#include "utils/logger.h"
/* pennfat.c */
// Global pointer to the currently mounted file system.
fat_st* current_fs = NULL;
char fs_name[256] = {0};

// Special routine: Create a new PennFAT filesystem.
// Usage: mkfs FS_NAME BLOCKS_IN_FAT (1~32) BLOCK_SIZE_CONFIG（0~4）
void s_mkfs(const char* fs_name_arg, int blocks_in_fat, int block_size_config) {
  if (current_fs != NULL) {
    const char* msg =
        "mkfs error! file system already mounted can't do more mkfs\n";
    write(STDERR_FD, msg, strlen(msg));
    return;
  }
  // second param, block_size_config（0~4）
  fat_st* fs_ptr = NULL;
  if (k_fat_init(&fs_ptr, block_size_config, blocks_in_fat) < 0) {
    const char* msg = "mkfs error! file system not able to init\n";
    write(STDERR_FD, msg, strlen(msg));
    return;
  }
  if (k_fat_save(fs_ptr, fs_name_arg) < 0) {
    const char* msg = "mkfs: Failed to save filesystem\n";
    write(STDERR_FD, msg, strlen(msg));
    // free the filesystem structure
    k_fat_unmount(fs_ptr, fs_name_arg);
    return;
  }
  k_fat_unmount(fs_ptr, fs_name_arg);
  const char* msg = "Filesystem created\n";
  write(STDERR_FD, msg, strlen(msg));
}

// Special routine: Mount a PennFAT filesystem.
// Usage: mount FS_NAME
int s_mount(const char* fs_name_arg) {
  if (current_fs != NULL) {
    s_printf(STDERR_FILENO, "mount: Filesystem already mounted\n");
    return -1;
  }
  if (k_fat_mount(&current_fs, fs_name_arg) < 0) {
    s_printf(STDERR_FD, "mount: Failed to mount filesystem %s\n", fs_name_arg);
    return -1;
  }

  // if mount successfully, load the directory entries from FS
  k_load_directory_entries(current_fs);

  strncpy(fs_name, fs_name_arg, sizeof(fs_name) - 1);
  // s_printf(STDERR_FD, "Filesystem mounted: %s\n", fs_name);
  dprintf(STDERR_FD, "Filesystem mounted: %s\n", fs_name);
  return 0;
}

// Special routine: Unmount the currently mounted filesystem.
// Usage: unmount
int s_unmount() {
  if (current_fs == NULL) {
    s_printf(STDERR_FD, "unmount: No filesystem mounted\n");
    return -1;
  }
  // clear the directory entries before unmounting to avoid stale data
  k_clear_directory_entries();
  if (k_fat_unmount(current_fs, fs_name) < 0) {
    s_printf(STDERR_FD, "unmount: Failed to unmount filesystem\n");
    return -1;
  }
  current_fs = NULL;
  s_printf(STDERR_FD, "Filesystem unmounted\n");
  return 0;
}

// Command: touch – create files or update modification time.
void s_touch(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FD, "touch: No filesystem mounted\n");
    return;
  }
  // args[0] is "touch"
  for (int i = 1; args[i] != NULL; i++) {
    if (k_fat_find(current_fs, args[i]) == K_FAT_FIND_FILE_NO_EXIST) {
      // s_printf(STDERR_FD, "touch: File %s does not exist, creating...\n",
      // args[i]);
      if (k_create_file(current_fs, args[i], 0, 6) < 0)
        s_printf(STDERR_FD, "touch: Failed to create file %s\n", args[i]);
    } else {
      k_update_file_mtime(current_fs, args[i], time(NULL));
    }
  }
}

// Command: cat – display file contents or read from terminal.
// If called as:
//    cat file1 file2 ...           => reads files and prints to stdout
//    cat -w output_file             => reads from terminal and overwrites
//    output_file cat -a output_file             => reads from terminal and
//    appends to output_file cat file1 ... fileN -w output_file cat file1 ...
//    fileN -a output_file
#define CAT_MODE_TO_STDOUT 0
#define CAT_MODE_OVERWRITE 1
#define CAT_MODE_APPEND 2

void s_cat(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FD, "cat: No filesystem mounted\n");
    return;
  }

  // Count the arguments (argc)
  int argc = 0;
  while (args[argc] != NULL) {
    terminal_logger_debug("s_cat: args[%d] = %s\n", argc, args[argc]);
    argc++;
  }

  // Default: print input files to stdout
  int mode = CAT_MODE_TO_STDOUT;  // 0: print to stdout, 1: overwrite output, 2:
                                  // append to output
  const char* output_file = NULL;
  int input_start = 1;   // Files to read start at args[1]
  int input_end = argc;  // Files to read end at args[argc-1]

  // Case 1: Only 2 arguments after "cat" and first arg is a flag.
  // E.g., "cat -w output_file" or "cat -a output_file" means no input files;
  // read from terminal.
  if (argc == 3 && (strcmp(args[1], "-w") == 0 || strcmp(args[1], "-a") == 0)) {
    mode = (strcmp(args[1], "-w") == 0) ? CAT_MODE_OVERWRITE : CAT_MODE_APPEND;
    output_file = args[2];
    input_start = argc;  // no input files provided
  }
  // Case 2: More than 2 arguments and the second-to-last argument is a flag.
  // E.g., "cat file1 file2 -w output_file" or "cat file1 file2 -a output_file"
  else if (argc >= 3 && (strcmp(args[argc - 2], "-w") == 0 ||
                         strcmp(args[argc - 2], "-a") == 0)) {
    mode = (strcmp(args[argc - 2], "-w") == 0) ? CAT_MODE_OVERWRITE
                                               : CAT_MODE_APPEND;
    output_file = args[argc - 1];
    input_end = argc - 2;  // the files are from args[1] to args[argc-3]
  }
  // Otherwise, no redirection flag: all arguments (after "cat") are input file
  // names.

  // If no input files are specified (input_start >= input_end), then read from
  // terminal.
  if (input_start >= input_end) {
    char buf[1024];
    if (mode != CAT_MODE_TO_STDOUT) {
      // For redirection mode, remove the output file if overwriting.
      if (mode == CAT_MODE_OVERWRITE) {
        // unlinking before creating the output file
        k_unlink(current_fs, output_file);
      }

      // Create the output file if it doesn't exist.
      if (k_fat_find(current_fs, output_file) == K_FAT_FIND_FILE_NO_EXIST) {
        s_printf(STDERR_FILENO, "start typing in your message to cat\n");
        if (k_create_file(current_fs, output_file, 0, 6) ==
            K_CREATE_FILE_ERROR) {
          s_printf(STDERR_FILENO, "cat: Failed to create output file %s\n",
                   output_file);
          return;
        }
      }
      // append mode is correct because overwrite mode is handled above and
      // cursor is reset to be 0 after unlink and create file
      int fd_out = k_open(current_fs, output_file,
                          F_APPEND);  // double check -> this used to be 1
      if (fd_out < 0) {
        s_printf(STDERR_FILENO, "cat: Failed to open output file %s\n",
                 output_file);
        return;
      }
      // Read exactly one line from stdin (stop at first Enter), then write it.
      if (s_fgets(buf, sizeof(buf), STDIN_FILENO) != NULL) {
        // Compute length (including the '\n')
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
          // Overwrite the newline with a dot, but keep len unchanged
          buf[len - 1] = ' ';
        }

        // If appending, move the cursor to the end of the file.
        if (mode == CAT_MODE_APPEND) {
          k_lseek(current_fs, fd_out, 0, SEEK_END);
        }
        // Write the entire buffer up through the dot
        k_write(current_fs, fd_out, buf, len);
      }
      // Close the output file descriptor
      k_close(current_fs, fd_out);
      k_update_root_directory(current_fs);
      k_fat_save(current_fs, fs_name);

      s_printf(STDERR_FILENO, "cat: Read from terminal and %s output file %s\n",
               (mode == 1) ? "overwrote" : "appended to", output_file);
    } else {
      // No redirection flag, simply copy from stdin to stdout.
      while (s_fgets(buf, sizeof(buf), STDIN_FILENO) != NULL) {
        if (strcmp(buf, "^c") == 0) {
          s_fputs("\n");
          break;
        }
        s_fputs(buf);
      }
    }
    return;
  }

  // Otherwise, we have one or more input files.
  int fd_out = -1;
  const char* output_file_name = output_file;
  if (mode != CAT_MODE_TO_STDOUT) {
    // For redirection mode, if overwrite, remove any previous output file.
    if (mode == CAT_MODE_OVERWRITE)
      k_unlink(current_fs, output_file);
    if (k_fat_find(current_fs, output_file) < 0) {
      if (k_create_file(current_fs, output_file, 0, 6) < 0) {
        s_printf(STDERR_FILENO, "cat: Failed to create output file %s\n",
                 output_file);
        return;
      }
    }
    fd_out = k_open(current_fs, output_file,
                    F_APPEND);  // this used to be 1
    if (fd_out < 0) {
      s_printf(STDERR_FILENO, "cat: Failed to open output file %s\n",
               output_file);
      return;
    }
  }

  // For each input file, open and read from the file.
  char buf[1024];
  for (int i = input_start; i < input_end; i++) {
    const char* input_file_name = args[i];
    int fd_in = k_open(current_fs, args[i], 0);
    if (fd_in < 0) {
      s_printf(STDERR_FILENO, "Error cat: Cannot open file %s\n", args[i]);
      return;
    }
    int read_num;
    while ((read_num = k_read(current_fs, fd_in, sizeof(buf), buf)) > 0) {
      if (mode == CAT_MODE_TO_STDOUT) {
        write(STDOUT_FILENO, buf, read_num);
      } else {
        // If appending, move the cursor to the end of the file.
        if (mode == CAT_MODE_APPEND) {
          // check if input and output are the same
          if (strcmp(input_file_name, output_file_name) == 0) {
            s_printf(
                STDERR_FILENO,
                "cat: input and output cannot be the same when appending\n");
            return;
          }
          // lseek the cursor to the end of the file
          k_lseek(current_fs, fd_out, 0, SEEK_END);
        }
        k_write(current_fs, fd_out, buf, read_num);
      }
    }

    if (read_num == 0) {
      if (mode == CAT_MODE_APPEND) {
        // check if input and output are the same
        if (strcmp(input_file_name, output_file_name) == 0) {
          s_printf(STDERR_FILENO,
                   "cat: input and output cannot be the same when appending\n");
          return;
        }
      }
    }
    k_close(current_fs, fd_in);
  }

  if (mode != CAT_MODE_TO_STDOUT) {
    k_close(current_fs, fd_out);
    k_update_root_directory(current_fs);
    k_fat_save(current_fs, fs_name);
    s_printf(STDERR_FILENO, "cat: Copied input file(s) to output file %s\n",
             output_file);
  } else {
    s_printf(STDOUT_FILENO, "\n");
  }
}

// Command: chmod – change file permissions (simulation).
void s_chmod(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "chmod: No filesystem mounted\n");
    return;
  }
  // Expected usage: chmod PERM FILE
  if (args[1] == NULL || args[2] == NULL) {
    s_printf(STDERR_FILENO, "Usage: chmod PERM FILE\n");
    return;
  }
  // 0) make sure only number
  for (char* p = args[1]; *p; p++) {
    if (*p < '0' || *p > '9') {
      s_printf(STDERR_FILENO, "chmod: invalid mode '%s'\n", args[1]);
      return;
    }
  }
  int perm = atoi(args[1]);
  // 0.1) 0< num <= 7
  if (perm < 0 || perm > 7) {
    s_printf(STDERR_FILENO, "chmod: invalid mode '%s'\n", args[1]);
    return;
  }

  // 1) update perm
  if (k_update_file_perm(current_fs, args[2], perm) < 0) {
    s_printf(STDERR_FILENO, "chmod: File %s not found\n", args[2]);
    return;
  }
  // 2) update mtime
  k_update_file_mtime(current_fs, args[2], time(NULL));
  // 3) writeback
  k_update_root_directory(current_fs);
  k_fat_save(current_fs, fs_name);
  terminal_logger_debug("chmod: Updated permissions for %s to %o\n", args[2],
                        perm);
}

// Command: mv – rename or move a file.
// ex: if you do /root/f1 to /root/f2, you are changing the name of f1 to f2
// ex: if you do /root/f1 to /root/folder1/f2, you are moving f1 to folder1 and
// renaming it to f2 if destination file exists, it will be overwritten. Aka
// change the name of the file to the destination name and then remove the
// destination file if destination file doesn't exist, change the file to the
// destination file name
void s_mv(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "mv: No filesystem mounted\n");
    return;
  }
  // Expected usage: mv SOURCE DEST
  if (args[1] == NULL || args[2] == NULL) {
    s_printf(STDERR_FILENO, "Usage: mv SOURCE DEST\n");
    return;
  }
  int fd_src = k_open(current_fs, args[1], 0);
  if (fd_src < 0) {
    s_printf(STDERR_FILENO, "Error: mv: Source file %s not found\n", args[1]);
    return;
  }
  // need to check read/write permission for the source file
  if ((k_file_permission(current_fs, args[1]) & 4) == 0) {
    s_printf(STDERR_FILENO, "mv: No read permission for source file %s\n",
             args[1]);
    k_close(current_fs, fd_src);
    return;
  }
  int fd_dest = k_open(current_fs, args[2], 1);
  if (fd_dest >= 0) {
    // destination file exists
    // s_printf(STDERR_FILENO, "mv: Destination file %s exists,
    // overwriting...\n",
    //          args[2]);

    // check if the destination file has write permission
    // if the destination file is opened, we need to check if it has write
    // permission
    if ((k_file_permission(current_fs, args[2]) & 2) == 0) {
      s_printf(
          STDERR_FILENO,
          "mv: file already exist, but does not have a write permission %s\n",
          args[2]);
      k_close(current_fs, fd_src);
      k_close(current_fs, fd_dest);
      return;
    }
    // remove the destination file
    k_close(current_fs, fd_dest);
    if (k_unlink(current_fs, args[2]) < 0) {
      s_printf(STDERR_FILENO,
               "mv: Failed to remove existing destination file %s\n", args[2]);
      k_close(current_fs, fd_src);
      return;
    }

  } else {
    // s_printf(STDERR_FILENO,
    //          "mv: Destination file %s does not exist, changing name...\n",
    //          args[2]);
  }
  // change the name of the source file to the destination file name
  if (k_update_file_name(current_fs, args[1], args[2]) < 0) {
    s_printf(STDERR_FILENO, "mv: Failed to update file %s\n", args[1]);
    k_close(current_fs, fd_src);
    return;
  }
  k_close(current_fs, fd_src);
}

// Command: rm – remove (unlink) files.
void s_rm(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "rm: No filesystem mounted\n");
    return;
  }
  // args[0] is "rm"
  for (int i = 1; args[i] != NULL; i++) {
    // s_printf(STDERR_FILENO, "rm: Removing file %s\n", args[i]);
    if (k_unlink(current_fs, args[i]) < 0) {
      // s_printf(STDERR_FILENO, "rm: Failed to remove file %s\n", args[i]);
    }
  }
}

// Command: cp – copy files between host and PennFAT or within PennFAT.
// Usage:
//   cp -h HOST_SRC DEST_FS      # Host → PennFAT
//   cp SRC_FS -h HOST_DEST      # PennFAT → Host
//   cp SRC_FS DEST_FS           # PennFAT → PennFAT
void s_cp(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "cp: No filesystem mounted\n");
    return;
  }

  // Count arguments
  int count = 0;
  while (args[count])
    count++;

  // Detect -h flag
  int host_source = (count == 4 && strcmp(args[1], "-h") == 0);
  int host_dest = (count == 4 && strcmp(args[2], "-h") == 0);

  // 1) Host → PennFAT
  if (host_source) {
    if (count != 4) {
      s_printf(STDERR_FILENO, "Usage: cp -h SOURCE DEST\n");
      return;
    }
    const char* src_host = args[2];
    const char* dst_fs = args[3];
    // check if the source file exists
    if (k_fat_find(current_fs, dst_fs) != K_FAT_FIND_FILE_NO_EXIST) {
      if ((k_file_permission(current_fs, dst_fs) & 2) == 0) {
        s_printf(STDERR_FILENO,
                 "cp: No write permission for destination file %s\n", dst_fs);
        return;
      }
    }
    FILE* fp_src = fopen(src_host, "rb");
    if (!fp_src) {
      s_printf(STDERR_FILENO, "cp: Failed to open host source file %s\n",
               src_host);
      return;
    }
    // check if the destination file exists
    // check if the destination file has write permission
    int fd_dest = k_open(current_fs, dst_fs, F_WRITE);
    if (fd_dest == K_OPEN_FILE_DOES_NOT_EXIST) {
      if (k_create_file(current_fs, dst_fs, 0, 6) < 0) {
        s_printf(STDERR_FILENO, "cp: Failed to create destination file %s\n",
                 dst_fs);
        fclose(fp_src);
        return;
      }
      fd_dest = k_open(current_fs, dst_fs, F_WRITE);
    }
    if (fd_dest < 0) {
      s_printf(STDERR_FILENO, "cp: Failed to open destination file %s\n",
               dst_fs);
      fclose(fp_src);
      return;
    }
    char buf[1024];
    int n;
    while ((n = fread(buf, 1, sizeof(buf), fp_src)) > 0) {
      k_write(current_fs, fd_dest, buf, n);
    }
    fclose(fp_src);
    k_close(current_fs, fd_dest);
    k_update_root_directory(current_fs);
    k_fat_save(current_fs, fs_name);
    s_printf(STDERR_FILENO, "cp: Copied host file %s to filesystem file %s\n",
             src_host, dst_fs);
  }
  // 2) PennFAT → Host
  else if (host_dest) {
    if (count != 4) {
      s_printf(STDERR_FILENO, "Usage: cp SOURCE -h DEST\n");
      return;
    }
    const char* src_fs = args[1];
    const char* dst_host = args[3];
    // check read perm
    if (k_fat_find(current_fs, src_fs) == K_FAT_FIND_FILE_NO_EXIST ||
        (k_file_permission(current_fs, src_fs) & 4) == 0) {
      s_printf(STDERR_FILENO, "cp: No read permission for source file %s\n",
               src_fs);
      return;
    }
    int fd_src = k_open(current_fs, src_fs, F_READ);
    if (fd_src < 0) {
      s_printf(STDERR_FILENO, "cp: Failed to open source file %s\n", src_fs);
      return;
    }
    FILE* fp_dest = fopen(dst_host, "wb");
    if (!fp_dest) {
      s_printf(STDERR_FILENO, "cp: Failed to open host destination file %s\n",
               dst_host);
      k_close(current_fs, fd_src);
      return;
    }
    char buf[1024];
    int n;
    while ((n = k_read(current_fs, fd_src, sizeof(buf), buf)) > 0) {
      fwrite(buf, 1, n, fp_dest);
    }
    k_close(current_fs, fd_src);
    fclose(fp_dest);
    s_printf(STDERR_FILENO, "cp: Copied filesystem file %s to host file %s\n",
             src_fs, dst_host);
  }
  // 3) PennFAT → PennFAT
  else {
    if (count != 3) {
      s_printf(STDERR_FILENO, "Usage: cp SOURCE DEST\n");
      return;
    }
    const char* src_fs = args[1];
    const char* dst_fs = args[2];
    // check read perm
    if (k_fat_find(current_fs, src_fs) == K_FAT_FIND_FILE_NO_EXIST) {
      s_printf(STDERR_FILENO, "cp: Source file %s not found\n", src_fs);
      return;
    }
    if ((k_file_permission(current_fs, src_fs) & 4) == 0) {
      s_printf(STDERR_FILENO, "cp: No read permission for source file %s\n",
               src_fs);
      return;
    }
    // check write perm
    if (k_fat_find(current_fs, dst_fs) != K_FAT_FIND_FILE_NO_EXIST &&
        (k_file_permission(current_fs, dst_fs) & 2) == 0) {
      s_printf(STDERR_FILENO,
               "cp: No write permission for destination file %s\n", dst_fs);
      return;
    }

    int fd_src = k_open(current_fs, src_fs, F_READ);
    if (fd_src < 0) {
      s_printf(STDERR_FILENO, "cp: Failed to open source %s\n", src_fs);
      return;
    }
    // check if the destination file exists
    if (k_fat_find(current_fs, dst_fs) == K_FAT_FIND_FILE_NO_EXIST) {
      int src_fs_perm = k_file_permission(current_fs, src_fs);
      if (k_create_file(current_fs, dst_fs, 0, src_fs_perm) < 0) {
        s_printf(STDERR_FILENO, "cp: Failed to create destination file %s\n",
                 dst_fs);
        k_close(current_fs, fd_src);
        return;
      }
    }
    // the file does exist
    int fd_dest = k_open(current_fs, dst_fs, F_WRITE);
    if (fd_dest < 0) {
      s_printf(STDERR_FILENO, "cp: Failed to open destination %s\n", dst_fs);
      k_close(current_fs, fd_src);
      return;
    }
    // I will have to check if the src_file and dest_file permission matches

    char buf[1024];
    int n;
    while ((n = k_read(current_fs, fd_src, sizeof(buf), buf)) > 0) {
      k_write(current_fs, fd_dest, buf, n);
    }
    k_close(current_fs, fd_src);
    k_close(current_fs, fd_dest);
    k_update_root_directory(current_fs);
    k_fat_save(current_fs, fs_name);
    s_printf(STDERR_FILENO, "cp: Copied filesystem file %s to %s\n", src_fs,
             dst_fs);
  }
}
// Command: ls – list files in the current directory.
char* s_fgets(char* buf, size_t maxlen, int fd) {
  ssize_t r = k_read_line(fd, buf, maxlen);
  if (r > 0)
    return buf;
  return NULL;
}

// Command: fputs – write a string to the specified file descriptor.
int s_fputs(const char* s) {
  // global fat_st* current_fs have point to current filesystem
  size_t len = strlen(s);
  return k_write(current_fs, STDOUT_FILENO, s, len);
}

///////////////// File System 2.5 code ////////////////////////////////

// Command: write – write n bytes from a string into fd
// Usage: write FD STR N
int s_write(int fd, const char* str, int n) {
  if (current_fs == NULL) {
    char* msg = "No filesystem mounted\n";
    write(STDERR_FILENO, msg, strlen(msg));
    errno = ENODEV;
    return -1;
  }

  int rc = k_write(current_fs, fd, str, n);
  if (rc < 0) {
    return -1;
  } else {
    terminal_logger_debug("write: %d bytes written to fd %d\n", rc, fd);
    // refresh
    k_update_root_directory(current_fs);
    k_fat_save(current_fs, fs_name);
    return rc;
  }
}
// Command: close – close the file descriptor.
int s_close(int fd) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "close: No filesystem mounted\n");
    errno = ENODEV;
    return -1;
  }
  // fd must >= 0
  if (fd < 0) {
    s_printf(STDERR_FILENO, "Usage: close FD\n");
    errno = EINVAL;
    return -1;
  }
  // cannot close 0,1,2
  if (fd >= 0 && fd <= 2) {
    s_printf(STDERR_FILENO, "close: cannot close standard descriptor %d\n", fd);
    errno = EINVAL;
    return -1;
  }
  int rc = k_close(current_fs, fd);
  if (rc < 0) {
    s_printf(STDERR_FILENO, "close: Failed to close fd %d: %s\n", fd,
             strerror(errno));
  }
  return 0;
}

// unlink – unlink (delete) the file，if there are other fd table inuse then
int s_unlink(const char* fname) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "unlink: No filesystem mounted\n");
    errno = ENODEV;
    return -1;
  }
  if (fname == NULL) {
    s_printf(STDERR_FILENO, "Usage: unlink FILE\n");
    errno = EINVAL;
    return -1;
  }
  int rc = k_unlink(current_fs, fname);
  if (rc < 0) {
    if (errno == EBUSY)
      s_printf(STDERR_FILENO, "unlink: File '%s' is currently in use\n", fname);
    else {
      s_printf(STDERR_FILENO, "unlink: Failed to delete '%s'\n", fname);
    }
    return -1;
  }
  return 0;
}

/**
 * reposition the file pointer for fd to the offset relative to whence.
 * Returns new offset on success, or -1 on error.
 */
int s_lseek(int fd, int offset, int whence) {
  if (!current_fs) {
    s_printf(STDERR_FILENO, "lseek: No filesystem mounted\n");
    errno = ENODEV;
    return -1;
  }

  // check whence
  if (whence != F_SEEK_SET && whence != F_SEEK_CUR && whence != F_SEEK_END) {
    s_printf(STDERR_FILENO, "lseek: invalid whence %d\n", whence);
    errno = EINVAL;
    return -1;
  }

  int rc = k_lseek(current_fs, fd, offset, whence);
  if (rc < 0) {
    s_printf(STDERR_FILENO, "lseek: error repositioning fd %d\n", fd);
    return -1;
  } else {
    s_printf(STDERR_FILENO, "New offset: %d\n", rc);
    return rc;
  }
}

/**
 * Command: read – read n bytes from fd into buf.
 * Usage: read FD N BUFFER
 */
int s_read(char** args) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "read: No filesystem mounted\n");
    errno = ENODEV;
    return -1;
  }
  // args[0] == "read", so we need args[1] (FD) and args[2] (N)
  if (args[1] == NULL || args[2] == NULL) {
    s_printf(STDERR_FILENO, "Usage: read FD N\n");
    errno = EINVAL;
    return -1;
  }
  int fd = atoi(args[1]);
  int n = atoi(args[2]);
  if (fd < 0 || n < 0) {
    s_printf(STDERR_FILENO, "read: invalid FD or N\n");
    errno = EINVAL;
    return -1;
  }

  char* buf = malloc(n);
  if (!buf) {
    s_printf(STDERR_FILENO, "read: memory allocation failed\n");
    errno = ENOMEM;
    return -1;
  }

  int rc = k_read(current_fs, fd, n, buf);
  if (rc < 0) {
    s_printf(STDERR_FILENO, "read: error reading from fd %d: %s\n", fd,
             strerror(errno));
    return -1;
  } else {
    s_printf(STDERR_FILENO, "read: %d bytes read from fd %d\n", rc, fd);

    return rc;
  }
  free(buf);
}

// Command: ls – list files in the filesystem.
void s_ls() {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "ls: No filesystem mounted\n");
    return;
  }
  k_ls();
}

// Command: open – open a file in the filesystem.
int s_open(const char* fname, int mode) {
  if (current_fs == NULL) {
    s_printf(STDERR_FILENO, "open: No filesystem mounted\n");
    return -1;
  }

  int fd = k_open(current_fs, fname, mode);
  if (fd == S_OPEN_FAILED) {
    s_printf(STDERR_FILENO, "open: Failed to open file %s\n", fname);
    return -1;
  }
  return fd;
}

/**
 * @brief Change permissions of a file by adding/removing bits.
 *
 * Example Usage:
 *   chmod +x file   // add executable
 *   chmod +rw file  // add read+write
 *   chmod -wx file  // rm write+exec
 */
void* u_chmod(void* arg) {
  char** args = (char**)arg;
  int argc = 0;
  while (args[argc])
    argc++;

  if (argc != 3) {
    s_printf(STDERR_FILENO, "Usage: chmod [+/-][rwx]+ FILE\n");
    s_exit();
    return NULL;
  }

  struct parsed_command* pcmd = NULL;
  char* joined_args = join_argv_tail(args, argc, 0);
  int parsed_result = parse_command(joined_args, &pcmd);
  if (parsed_result < 0) {
    s_printf(STDERR_FILENO, "u_chmod: Failed to parse command\n");
    s_exit();
    return NULL;
  }

  if (!pcmd || !pcmd->commands[0]) {
    s_printf(STDERR_FILENO, "u_chmod: Invalid arguments\n");
    s_exit();
    return NULL;
  }

  const char* mode = args[1];
  const char* file = args[2];

  if (current_fs == NULL ||
      k_fat_find(current_fs, file) == K_FAT_FIND_FILE_NO_EXIST) {
    s_printf(STDERR_FILENO, "chmod: File %s not found\n", file);
    s_exit();
    return NULL;
  }

  int curr = k_file_permission(current_fs, file);
  if (curr < 0) {
    s_printf(STDERR_FILENO, "chmod: Cannot read permissions for %s\n", file);
    s_exit();
    return NULL;
  }

  int add;
  if (mode[0] == '+') {
    add = 1;
  } else if (mode[0] == '-') {
    add = 0;
  } else {
    s_printf(STDERR_FILENO, "chmod: Invalid mode '%s'\n", mode);
    s_exit();
    return NULL;
  }
  // check if the mode is valid
  for (const char* p = mode + 1; *p; ++p) {
    int bit;
    switch (*p) {
      case 'r':
        bit = 4;
        break;
      case 'w':
        bit = 2;
        break;
      case 'x':
        bit = 1;
        break;
      default:
        s_printf(STDERR_FILENO, "chmod: Invalid flag '%c'\n", *p);
        s_exit();
        return NULL;
    }
    if (add) {
      curr |= bit;
    } else {
      curr &= ~bit;
    }
  }

  // call s chmod
  char perm_str[4];
  sprintf(perm_str, "%o", curr);
  char* s_argv[] = {"chmod", perm_str, (char*)file, NULL};
  s_chmod(s_argv);
  s_exit();
  return NULL;
}
// our custom dup2 – swaps file descriptors
int s_dup2(int fd1, int fd2, pid_t pid, const char* argv) {  // fd2 = ouptput fd
  if (k_dup2(current_fs, fd1, fd2, pid, argv) < 0) {
    s_printf(STDERR_FILENO, "dup2: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}