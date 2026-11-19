// user/u_filesystemfunc.h
#ifndef U_FILESYSTEMFUNC_H
#define U_FILESYSTEMFUNC_H

#include "../utils/parser.h"  // for struct parsed_command

/**
 * @brief The ususal `cat` program.
 *
 * If `files` arg is provided, concatenate these files and print to stdout
 * If `files` arg is *not* provided, read from stdin and print back to stdout
 *
 * Example Usage: cat f1 f2 (concatenates f1 and f2 and print to stdout)
 * Example Usage: cat f1 f2 < f3 (concatenates f1 and f2 and prints to stdout,
 * ignores f3) Example Usage: cat < f3 (concatenates f3, prints to stdout)
 */
void* u_cat(void* arg);

/**
 * @brief Echo back an input string.
 *
 * Example Usage: echo Hello World
 */
void* u_echo(void* arg);

/**
 * @brief Lists all files in the working directory.
 * For extra credit, it should support relative and absolute file paths.
 *
 * Example Usage: ls (regular credit)
 * Example Usage: ls ../../foo/./bar/sample (only for EC)
 */
void* u_ls(void* arg);

/**
 * @brief For each file, create an empty file if it doesn't exist, else update
 * its timestamp.
 *
 * Example Usage: touch f1 f2 f3 f4 f5
 */
void* u_touch(void* arg);

/**
 * @brief Rename a file. If the `dst_file` file already exists, overwrite it.
 *
 * Print appropriate error message if:
 * - `src_file` is not a file that exists
 * - `src_file` does not have read permissions
 * - `dst_file` file already exists but does not have write permissions
 *
 * Example Usage: mv src_file dst_file
 */
void* u_mv(void* arg);

/**
 * Copy a file. If the `dst_file` file already exists, overwrite it.
 *
 * Print appropriate error message if:
 * - `src_file` is not a file that exists
 * - `src_file` does not have read permissions
 * - `dst_file` file already exists but does not have write permissions
 *
 * Example Usage: cp src_file dst_file
 */
void* u_cp(void* arg);

/**
 * @brief Remove a list of files.
 * Treat each file in the list as a separate transaction. (i.e. if removing
 * file1 fails, still attempt to remove file2, file3, etc.)
 *
 * Print appropriate error message if:
 * - `file` is not a file that exists
 *
 * Example Usage: rm f1 f2 f3 f4 f5
 */
void* u_rm(void* arg);

/**
 * @brief Change permissions of a file.
 * There's no need to error if a permission being added already exists, or
 * if a permission being removed is already not granted.
 *
 * Print appropriate error message if:
 * - `file` is not a file that exists
 * - `perms` is invalid
 *
 * Example Usage: chmod +x file (adds executable permission to file)
 * Example Usage: chmod +rw file (adds read + write permissions to file)
 * Example Usage: chmod -wx file (removes write + executable permissions from
 * file)
 */
void* u_chmod(void* arg);

#endif  // U_FILESYSTEMFUNC_H
