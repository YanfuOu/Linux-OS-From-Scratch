#ifndef K_FILE_SYSTEM_H
#define K_FILE_SYSTEM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include "../system/s_io_marco.h"
#include "../utils/Vec.h"

#define MAX_OPEN_FILES 1024
#define MAX_FDS_PER_PROCESS 1024
#define END_OF_CHAIN ((block_num_t)0xFFFF)

#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

// The block number type in the FAT table
// The block number is the index of the block in the FAT table
typedef uint16_t block_num_t;

// The entry of the FAT table
// The FAT table is a vector of the FAT entries
// Each FAT entry contains the block number of the next block
// For the last block in a file, the next block number is the last block number
// For the free block, the next block number is 0
typedef struct fat_entry_st {
  // takes up 2 bytes in the FAT table
  block_num_t next_block;  // next block in the chain, END_OF_CHAIN (-1) if it
                           // is the last block
} fat_entry_t;

// The header of the FAT table
// The header contains the block size and the block count
// The block size is the size of each block in the FAT table
// The block count is the total number of blocks in the FAT table
// It should be stored in the first block of the FAT table, i.e. FAT[0]
typedef struct fat_header_st {
  uint8_t block_size;
  uint8_t block_count;
} fat_header_t;

// The FAT table
// The FAT table contains the header and the entries
typedef struct fat_st {
  // the first block of the FAT table, which contains metadata of the FAT table
  fat_header_t header;
  // The array of FAT entries, the first entry (index 0) is the root directory,
  // the rest of the entries are used for data blocks or free blocks
  fat_entry_t* entries;
  // store the data block
  char* data_blocks;  // This is used to store the actual data blocks in memory
  // void* fat_table_addr;
} fat_st;

// The directory entry (saves the file metadata)
typedef struct directory_entry_st {
  char name[32];
  uint32_t size;
  uint16_t first_block;
  uint8_t type;
  uint8_t perm;
  time_t mtime;
  char reserved[16];
} directory_entry_t;

// The file control block
// It is used to manage the file used in the global file table
typedef struct file_control_block_st {
  char name[32];
  uint32_t size;
  uint16_t first_block;
  uint8_t type;
  uint8_t perm;
  // Last modified time
  time_t mtime;
  // # of FDs that are pointing to this file
  int ref_count;
  // Offset in the file
  off_t offset;
  // Synced with the disk or not
  int synced;
} file_control_block_t;

// The global file table entry
typedef struct global_file_table_entry_st {
  file_control_block_t* fcb;
  int in_use;
} global_file_table_entry_t;

// Global File Table
// It is used to manage the files that are opened by the processes
// It is a vector of the global file table entries
// Each entry contains the file control block and the status of the entry
typedef struct global_file_table_st {
  // All the array here can be replaced by a vector
  global_file_table_entry_t entries[MAX_OPEN_FILES];
  int open_file_count;
} global_file_table_t;

// The entry in the process file descriptor table
// It contains the file index in the global file table and the cursor in the
// file The cursor is the offset in the file of the current process.
typedef struct proc_fd_entry_st {
  int file_index;
  // The offset in the file of the current process
  // Should we also maintain the cursor for each file descriptor in the
  // process regarding there may be different processes reading a same file with
  // different cursors [Ask TA / Ed]
  off_t cursor;
} proc_fd_entry_t;

// Process File Descriptor Table for each process, which is separated
typedef struct proc_fd_table_st {
  proc_fd_entry_t entries[MAX_FDS_PER_PROCESS];
  int open_file_count;
  // entries[1] = {file_index: 0}, 1 here is the file descriptor
  // entries[2] = {file_index: 1}, 2 here is the file descriptor
} proc_fd_table_t;

/**
 * Definition of an entry in the proc_fd_table
 * Each entry corresponds to a complete file descriptor table for a process.
 */
typedef struct proc_table_entry_st {
  pid_t proc_id;
  proc_fd_table_t fd_table;
} proc_table_entry_t;

/**
 * A vector containing all the live processes in the system.
 */
extern Vec proc_fd_table;

// File Sytem kernel level functions

/*!
 * Open a file with the given path and mode
 * Return the file descriptor if success, -1 if failed
 * @param fat: the file system
 * @param file_name: the name of the file
 * @param mode: mode of the file, 0 for read, 1 for append, 2 for write
 * F_WRITE - writing and reading, truncates if the file exists, or creates it if
 it does not exist. Only one instance of a file can be opened in F_WRITE mode;
 error if attempted to open a file in F_WRITE mode more than once
 * F_READ - open the file for reading only, return an error if the file does not
 exist
 * F_APPEND - open the file for reading and writing but does not truncate the
 file if exists; additionally, the file pointer references the end of the file.

 * @return the file descriptor if success, -1 if failed
 */
int k_open(fat_st* fat, const char* path, int mode);

/*!
 * Read n bytes from the file with the given file descriptor
 * Return the number of bytes read if success, -1 if failed
 * @param fat: the file system
 * @param fd: the file descriptor
 * @param n: the number of bytes to read
 * @param buf: the buffer to store the read bytes
 * @return the number of bytes read if success, -1 if failed
 */
int k_read(fat_st* fat, int fd, int n, char* buf);

/*!
 * Write n bytes to the file with the given file descriptor
 * Return the number of bytes written if success, -1 if failed
 * @param fat: the file system
 * @param fd: the file descriptor
 * @param str: the string to write
 * @param n: the number of bytes to write
 * @return the number of bytes written if success, -1 if failed
 */
int k_write(fat_st* fat, int fd, const char* str, int n);

/*!
 * Close the file with the given file descriptor
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param fd: the file descriptor
 * @return 0 if success, -1 if failed
 */
int k_close(fat_st* fat, int fd);

/*!
 * Unlink the file with the given path, which is used to delete a file
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @return 0 if success, -1 if failed
 */
int k_unlink(fat_st* fat, const char* path);

/*!
 * Lseek the file with the given file descriptor
 * Return the offset if success, -1 if failed
 * @param fat: the file system
 * @param fd: the file descriptor
 * @param offset: the offset to seek
 * @param whence: the way to seek, 0 for SEEK_SET, 1 for SEEK_CUR, 2 for
 * SEEK_END
 * @return the offset if success, -1 if failed
 */
int k_lseek(fat_st* fat, int ft, int offset, int whence);

/*!
 * List the files in the given path
 * Return 0 if success, -1 if failed
 * @return 0 if success, -1 if failed
 */
int k_ls();

// int k_mkdir(const char* path);

// int k_rmdir(const char* path);

// int k_chdir(const char* path);

/*!
 * Initialize the file system with the given block size and block count
 * It can be used as the kernel level function for the mkfs command
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param block_size: the size of each block
 * @param block_count: the total number of blocks
 * @return 0 if success, -1 if failed
 */
int k_fat_init(fat_st** fat, int block_size, int block_count);

/*!
 * Mount the file system with the given path from the disk
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file system
 * @return 0 if success, -1 if failed
 */
int k_fat_mount(fat_st** fat, const char* path);

/*!
 * Unmount the file system
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file system to be saved
 * @return 0 if success, -1 if failed
 */
int k_fat_unmount(fat_st* fat, const char* path);

/*!
 * Allocate a free block in the FAT table
 * Return the block number if success, -1 if failed
 * @param fat: the file system
 * @return the block number if success, -1 if failed
 */
int k_fat_allocate_block(fat_st* fat);

/*!
 * Check if there is a free block in the FAT table
 * Return 1 if there is a free block, 0 if not
 * @param fat: the file system
 * @return 1 if there is a free block, 0 if not
 */
#define NO_FREE_BLOCKS -1
#define AVALIABLE_FREE_BLOCKS 1
int k_fat_able_to_allocate_block(fat_st* fat);

/*!
 * Free a block in the FAT table
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to be freed, need convertion
 * @return 0 if success, -1 if failed
 */
int k_fat_free_block(fat_st* fat, block_num_t block_num);

/*!
 * Write n bytes to the file with the given block number
 * Return the number of bytes written if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to write
 * @param buf: the buffer to write
 * @param num_bytes: the number of bytes to write
 * @param mode: the mode to write, 0 for overwrite, 1 for append
 * @return the number of bytes written if success, -1 if failed
 */

int k_fat_write(fat_st* fat,
                block_num_t block_num,
                const char* buf,
                int num_bytes,
                int offset);

/*!
 * Read n bytes from the file with the given block number
 * Return the number of bytes read if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to read
 * @param buf: the buffer to store the read bytes
 * @param num_bytes: the number of bytes to read
 * @return the number of bytes read if success, -1 if failed
 */
int k_fat_read(fat_st* fat, block_num_t block_num, char* buf, int num_bytes);

/*!
 * Save the file system to the disk (a bin file) with the given path
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file system to be saved
 * @return 0 if success, -1 if failed
 */
int k_fat_save(fat_st* fat, const char* path);

/*!
 * Find the block number of the file with the given path
 * Return the block number if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @return the block number if success, -1 if failed
 */
#define K_FAT_FIND_FILE_NO_EXIST -1
int k_fat_find(fat_st* fat, const char* path);

/*!
 * Extend the file with the given block number by one block
 * Return the new block number if success, -1 if failed
 * @param fat: the file system
 * @param last_block_num: the block number of the last block
 * @return the new block number if success, -1 if failed
 */
block_num_t k_fat_extend_one_block(fat_st* fat, block_num_t last_block_num);

/*!
 * Add a block after the block with the given block number
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to add after
 * @param new_block_num: the new block number to add
 * @return 0 if success, -1 if failed
 */
int k_fat_add_block(fat_st* fat,
                    block_num_t block_num,
                    block_num_t new_block_num);

/*!
 * Get the next block number of the block with the given block number
 * Return the next block number if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to get the next block
 * @return the next block number if success, -1 if failed
 */
block_num_t k_fat_get_next_block(fat_st* fat, block_num_t block_num);

/*!
 * Free the chain of blocks starting from the block with the given block number
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param block_num: the block number to start the chain
 * @return 0 if success, -1 if failed
 */
int k_fat_free_chain(fat_st* fat, block_num_t block_num);

/*!
 * Get the file control block of the file with the given path
 * under a directory entry using the FAT.
 * Return the file control block if success, NULL if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @return the file control block if success, NULL if failed
 */
file_control_block_t* k_get_file(fat_st* fat, const char* path);

/*!
 * Create a file with the given path, type, and permission
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param file_name: the name of the file to create
 * @param type: the type of the file, 0 for regular file, 1 for directory
 * @param perm: the permission of the file
 * @return 0 if success, -1 if failed
 */
#define K_CREATE_FILE_ERROR -1
#define K_CREATE_FILE_SUCCESS 0
int k_create_file(fat_st* fat,
                  const char* file_name,
                  uint8_t type,
                  uint8_t perm);

/*!
 * Remove a file with the given path
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @return 0 if success, -1 if failed
 */
int k_remove_file(fat_st* fat, const char* path);

/*!
 * Update the size of the file with the given path
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @param size: the new size of the file
 * @return 0 if success, -1 if failed
 */
int k_update_file_size(fat_st* fat, const char* path, uint32_t size);

/*!
 * Update the mtime of the file with the given path
 * Return 0 if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @param mtime: the new mtime of the file
 * @return 0 if success, -1 if failed
 */
int k_update_file_mtime(fat_st* fat, const char* path, time_t mtime);

/*!
 * Update the name of the file with the given path
 * Return 0 if success, -1 if failed
 * @param path: the path of the file
 * @param new_path: the new name of the file
 * @return 0 if success, -1 if failed
 */
int k_update_file_name(fat_st* fat, const char* path, const char* new_path);

/*!
 * Get the permission of the file with the given path
 * Return the permission if success, -1 if failed
 * @param fat: the file system
 * @param path: the path of the file
 * @return the permission if success, -1 if failed
 */
int k_file_permission(fat_st* fat, const char* path);

/*!
 * Find the directory entry in the data region of the FAT table
 * Return the pointer to the directory entry if success, NULL if failed
 * @param fat: the file system
 * @param target: the target directory entry to find
 * @return the pointer to the directory entry if success, NULL if failed
 */
// directory_entry_t* k_find_directory_entry_in_data_region(
//     fat_st* fat,
//     directory_entry_t* target);

/*!
 * Put the file name in the root directory
 * Return 0 if success, -1 if failed (not done this yet)
 * @param fat: the file system
 * @return 0 if success, -1 if failed
 */
void k_update_root_directory(fat_st* fat);

/**
 * @brief Clear the in-memory directory entry list.
 *
 * Resets the global directory count to zero, effectively discarding all cached
 * directory entries.
 */
void k_clear_directory_entries(void);

/**
 * @brief Zero out all data blocks used by the root directory in the FAT.
 *
 * Iterates the linked‐list of data blocks starting at block 1, the root
 * directory, and fills each block with zeros.
 *
 * @param fat  Pointer to the FAT filesystem structure.
 */
void k_clear_directory_data_blocks(fat_st* fat);

/**
 * @brief Load all root directory entries from the FAT into memory.
 *
 * Reads each data block of the root directory starting at block 1, parses
 * its fixed‐size directory_entry_t records until a name[0]=='\0' marker,
 * and populates the global g_directory_entries array.
 *
 * @param fat  Pointer to the FAT filesystem structure.
 */
void k_load_directory_entries(fat_st* fat);

/**
 * @brief Read up to maxlen-1 bytes from fd into buf, stopping at newline or
 * EOF, and null-terminate the result.
 *
 * @param fd      File descriptor to read from.
 * @param buf     Buffer to store the characters.
 * @param maxlen  Total size of buf, including space for '\0'.
 * @return
 *   >0 : number of bytes placed into buf (excluding terminating '\0'),
 *    0 : EOF before any data was read,
 *   -1 : an error occurred (errno is set).
 */
ssize_t k_read_line(int fd, char* buf, size_t maxlen);

/**
 * @brief Check if a given filename is valid according to POSIX rules.
 *
 * Valid characters include alphanumeric characters, '.', '-', and '_'. Any
 * other character is considered invalid.
 *
 * @param name  The filename to check.
 * @return true if the filename is valid, false otherwise.
 */
bool k_is_valid_posix_filename(const char* name);

/**
 * @brief Update the permission bits of a file in the FAT directory entries.
 *
 * This function searches the global directory table for an entry whose name
 * matches the provided path. If a match is found, it assigns the provided
 * permission byte to that entry’s `perm` field.
 *
 * @param fat   Pointer to the FAT filesystem context (currently unused).
 * @param path  Null-terminated string specifying the filename or path to
 * update.
 * @param perm  New permission bits to set on the file (e.g., read/write flags).
 * @return      0 if the permissions were successfully updated,
 *             -1 if no matching entry was found.
 */
int k_update_file_perm(fat_st* fat, const char* path, uint8_t perm);

/**
 * @brief Duplicate an existing file descriptor to a specified descriptor
 * number.
 *
 * This function makes `newfd` refer to the same open file as `oldfd`.
 * If `newfd` is already open, it is first closed. Both descriptors will share
 * the same file control block and the reference count is incremented.
 * If `oldfd == newfd`, no action is taken and `newfd` is returned.
 *
 * @param fat   Pointer to the FAT filesystem context.
 * @param oldfd The file descriptor to duplicate; must be within [0,
 * MAX_FDS_PER_PROCESS).
 * @param newfd The target descriptor number; must be within [0,
 * MAX_FDS_PER_PROCESS).
 * @param pid   The process ID of the calling process.
 * @return      The new descriptor (`newfd`) on success, or -1 on error
 *              (invalid descriptors or `oldfd` not open).
 */
int k_dup2(fat_st* fat, int oldfd, int newfd, pid_t pid, const char* argv);

/**
 * @brief Get the index of the current process in the global process file
 * descriptor table.
 *
 * This function retrieves the index of the current process ID in the global
 * process file descriptor table. The index is used to access the file
 * descriptor table for the current process.
 *
 * @param pid The process ID to search for in the global process file
 *
 * @return The index of the current process ID in the global process file
 *         descriptor table.
 *         Returns -10 if the process ID is not found in the table.
 */
int k_get_fd_table_index_by_pid(pid_t pid);

#endif  // K_FILE_SYSTEM_H

// Procedure of reading the FAT table into memory:
// 1. Read the first block of the FAT FAT[0] to get the header, calculating
// the size of the entire FAT table.
// 2. Read the entire FAT table into memory by using mmap function, mapping
// the entire FAT table to the memory
// 3. Everytime we need to check the FAT table entry, we can directly access
// the memory by using the initial address of the FAT table, plus the block
// number times the size of the FAT entry, then we get the address of the
// FAT entry
