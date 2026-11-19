#ifndef PENNFAT_H
#define PENNFAT_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kernel/k_file_system.h"
#include "kernel/k_proc.h"
#include "kernel/k_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Special flags for s_cat */
#define CAT_MODE_TO_STDOUT 0
#define CAT_MODE_OVERWRITE 1
#define CAT_MODE_APPEND 2

/**
 * @brief Create a new PennFAT filesystem file.
 * Usage: mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG
 * If a filesystem is already mounted, prints an error and returns immediately.
 * @param fs_name_arg Name of the filesystem image file to create.
 * @param blocks_in_fat Number of FAT blocks (1–32).
 * @param block_size_config Block size config (0–4 corresponding to
 * 256,512,1024,2048,4096 bytes).
 */
void s_mkfs(const char* fs_name_arg, int blocks_in_fat, int block_size_config);

/**
 * @brief Mount a PennFAT filesystem image.
 * Usage: mount FS_NAME
 * Loads the FAT into memory and populates directory entries.
 * @param fs_name_arg Name of the filesystem image file to mount.
 * @return 0 on success, -1 on failure.
 */
int s_mount(const char* fs_name_arg);

/**
 * @brief Unmount the currently mounted PennFAT filesystem.
 * Usage: unmount
 * Saves any pending changes and frees resources.
 * @return 0 on success, -1 on failure.
 */
int s_unmount(void);

/**
 * @brief Create or update file modification time.
 * Usage: touch FILE [...]
 * Creates files if they do not exist or updates mtime.
 * @param args NULL-terminated array of arguments: args[0]="touch", args[1...]
 * filenames.
 */
void s_touch(char** args);

/**
 * @brief Concatenate and display or redirect file contents.
 * Usage:
 *   cat [FILES...]             (print to stdout)
 *   cat -w OUTPUT_FILE         (read stdin, overwrite)
 *   cat -a OUTPUT_FILE         (read stdin, append)
 *   cat FILES... -w OUTPUT_FILE or -a OUTPUT_FILE
 * @param args NULL-terminated array of arguments starting with "cat".
 */
void s_cat(char** args);

/**
 * @brief Change file permissions (simulation).
 * Usage: chmod PERM FILE
 * Updates file modification time to simulate permission change.
 * @param args NULL-terminated array: args[0]="chmod", args[1]=perm,
 * args[2]=filename.
 */
void s_chmod(char** args);

/**
 * @brief Rename or move a file within the filesystem.
 * Usage: mv SOURCE DEST
 * Overwrites DEST if it exists, or renames SOURCE to DEST.
 * @param args NULL-terminated array: args[0]="mv", args[1]=src, args[2]=dest.
 */
void s_mv(char** args);

/**
 * @brief Remove (unlink) one or more files.
 * Usage: rm FILE [...]
 * @param args NULL-terminated array: args[0]="rm", args[1...] filenames to
 * remove.
 */
void s_rm(char** args);

/**
 * @brief Copy files between host OS and PennFAT or within PennFAT.
 * Usage:
 *   cp [-h] SOURCE DEST
 *   cp SOURCE [-h] DEST
 * Without -h: copy within PennFAT.
 * With -h: host↔filesystem operations.
 * @param args NULL-terminated array of arguments.
 */
void s_cp(char** args);

/**
 * @brief Read a line of input from a file descriptor into a buffer.
 * Usage:
 *   my_fgets(buf, maxlen, fd);
 * Reads up to maxlen-1 bytes, stops on newline or EOF, and null‑terminates.
 * @param buf     Buffer to store the resulting string (including '\\n' if
 * present).
 * @param maxlen  Total size of buf, including space for the terminating '\\0'.
 * @param fd      File descriptor to read from (e.g., STDIN).
 * @return        buf on success (>=1 byte read), or NULL on EOF or error.
 */
char* s_fgets(char* buf, size_t maxlen, int fd);

/**
 * @brief Write a null-terminated string to standard output.
 *
 * This function sends the entire contents of the given string `s` to the
 * standard output (file descriptor STDOUT_FILENO) by invoking the
 * filesystem‐aware `k_write` on the currently mounted FAT filesystem.
 *
 * @param s  Pointer to the null-terminated string to be written.
 * @return   On success, the number of bytes written (length of `s`);
 *           on error, a negative value is returned.
 */
int s_fputs(const char* s);

/**
 * @brief Open a file name fname with the mode mode and return a file
 * descriptor.
 *
 * The allowed modes are as follows:
 *
 * F_WRITE
 * - writing and reading, truncates if the file exists, or creates it if
 * it does not exist. Only one instance of a file can be opened in F_WRITE mode;
 * error if PennOS attempts to open a file in F_WRITE mode more than once
 *
 * F_READ
 * - open the file for reading only, return an error if the file does not exist
 *
 * F_APPEND
 * - open the file for reading and writing but does not truncate the
 * file if exists; additionally, the file pointer references the end of the
 * file.
 */
#define S_OPEN_FAILED -1
int s_open(const char* fname, int mode);

/////////// 2.5 FAT system code  //////////////////

/**
 * @brief Reposition the file pointer for an open file descriptor.
 *
 * This function verifies that a filesystem is mounted, validates the `whence`
 * parameter, and delegates the seek operation to `k_lseek`. On success, it
 * prints the new offset to stderr and returns it. On failure, it prints an
 * appropriate error message and returns -1, with `errno` set accordingly.
 *
 * @param fd      The file descriptor to reposition.
 * @param offset  The offset to set, interpreted according to `whence`.
 * @param whence  One of:
 *                - F_SEEK_SET: offset from the start of the file,
 *                - F_SEEK_CUR: offset from the current position,
 *                - F_SEEK_END: offset from the end of the file.
 * @return        The new file offset on success; -1 on error.
 */
int s_lseek(int fd, int offset, int whence);

/**
 * @brief Remove (unlink) a file from the current FAT filesystem.
 *
 * This function attempts to delete the file named by `fname` from the mounted
 * filesystem. It first verifies that a filesystem is mounted and that `fname`
 * is non-NULL. It then calls `k_unlink` to perform the actual deletion.
 * If the file is currently open (in use by one or more file descriptors),
 * `k_unlink` will fail with `errno == EBUSY`, and this function will print
 * a specific “file in use” error. For any other failure, a generic delete
 * failure message is printed.
 *
 * @param fname  Null-terminated string naming the file to delete.
 * @return       0 on success;
 *              -1 on error, with `errno` set to:
 *                - ENODEV if no filesystem is mounted
 *                - EINVAL if `fname` is NULL
 *                - EBUSY  if the file is currently in use
 *                - other error codes as returned by `k_unlink`
 */
int s_unlink(const char* fname);

/**
 * @brief Close an open file descriptor on the current FAT filesystem.
 *
 * This function verifies that a filesystem is mounted and that the provided
 * descriptor is valid and not one of the standard descriptors (0, 1, 2).
 * It then delegates the close operation to `k_close`. On failure, an error
 * message is printed to stderr.
 *
 * @param fd  The file descriptor to close; must be ≥ 3.
 * @return    0 on success; -1 on error, with `errno` set to:
 *            - ENODEV if no filesystem is mounted
 *            - EINVAL if `fd` is negative or refers to a standard descriptor
 *            - other errors as returned by `k_close`
 */
int s_close(int fd);

/**
 * @brief Write a specified number of bytes from a string to a file descriptor.
 *
 * This function ensures a FAT filesystem is mounted and validates the
 * provided file descriptor, buffer pointer, and byte count. It then calls
 * `k_write` to perform the actual write. On success, it logs the number of
 * bytes written, updates the root directory, saves the FAT metadata, and
 * returns the byte count. On failure, it prints an error message to stderr.
 *
 * @param fd   The file descriptor to write to.
 * @param str  Pointer to the buffer containing the data to write.
 * @param n    The number of bytes to write from `str`.
 * @return     The number of bytes written on success; -1 on error, with errno
 * set to:
 *             - ENODEV if no filesystem is mounted
 *             - EINVAL if arguments are invalid
 *             - other error codes as returned by `k_write`
 */
int s_write(int fd, const char* str, int n);

/**
 * @brief Read a specified number of bytes from a file descriptor.
 *
 * This function parses its arguments to obtain a file descriptor and byte
 * count, allocates a temporary buffer of size `n`, and calls `k_read` to
 * read up to `n` bytes from the open file. It prints the number of bytes
 * actually read or an appropriate error message, then returns the read count.
 *
 * @param args  Null-terminated array of C-strings:
 *              - args[0] must be "read"
 *              - args[1] is the file descriptor (as a decimal string)
 *              - args[2] is the number of bytes to read (as a decimal string)
 * @return      On success, the number of bytes read (>= 0);
 *              on error, -1, with `errno` set to:
 *                - ENODEV if no filesystem is mounted
 *                - EINVAL if arguments are missing or invalid
 *                - ENOMEM if memory allocation fails
 *                - other error codes as returned by `k_read`
 */
int s_read(char** args);

/**
 * @brief List all files in the current FAT filesystem.
 *
 * This function checks that a filesystem is mounted. If no filesystem is
 * mounted, it prints an error message to stderr. Otherwise, it delegates
 * to `k_ls`, which outputs the directory entries (filenames, attributes,
 * sizes, etc.) to stdout.
 */
void s_ls(void);

/**
 * @brief Duplicate a file descriptor, reporting errors to stderr.
 *
 * This function wraps the filesystem-aware `k_dup2`, using the currently
 * mounted filesystem. It attempts to make `fd2` refer to the same file as
 * `fd1`. On failure, it prints the corresponding error message to stderr.
 *
 * @param fd1  The source file descriptor to duplicate.
 * @param fd2  The target file descriptor number.
 * @param pid  The process ID of the calling process.
 * @return     0 on success; -1 on error (with `errno` set and an error
 *             message printed).
 */
int s_dup2(int fd1, int fd2, pid_t pid, const char* argv);

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

#ifdef __cplusplus
}
#endif

#endif  // PENNFAT_H
