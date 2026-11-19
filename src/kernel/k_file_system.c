// Author : Jimmy gime921
// Author: Yanfu Ou
/* k_file_system.c */
#include "k_file_system.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../system/pennfat.h"
#include "../system/s_io_marco.h"
#include "../system/sys_calls.h"

#define MAX_FILES 1024  // Maximum number of files in our flat directory
#define TOTAL_BLOCKS 128

// Global flat directory for file metadata.
static directory_entry_t g_directory_entries[MAX_FILES];
static int g_directory_count = 0;

extern char fs_name[];

// Global file table for open files.
static global_file_table_t global_file_table = {.open_file_count = 0};

// Global file descriptor table for all the processes.
// this basically works like a lookup table of pid_t pid <-> proc_fd_table
// fd_table where each process has its own fd table within this Vec ex:  2 <->
// fd_table(0, 1, 2, f1, f2, f3, etc) ex:  3 <-> fd_table(0, 1, 2, f5, f6, f7,
// etc)

Vec proc_fd_table;

// Process file descriptor table (simulate a single process).
// static proc_fd_table_t proc_fd_table;

// Helper function: Convert a block number to its corresponding address in the
// data area.
static inline char* block_address(fat_st* fat,
                                  int actual_block_size,
                                  block_num_t block_num) {
  return (char*)fat->data_blocks + ((block_num - 1) * actual_block_size);
}

// Initialize the process file descriptor table (set file_index to -1).
__attribute__((constructor)) static void init_proc_fd_table() {
  proc_fd_table = vec_new(0, NULL);
}

/*==========================================================================
   FAT Table and Filesystem Initialization Functions
  ==========================================================================*/
// initialize the FAT table and filesystem
int k_fat_init(fat_st** fat, int block_size_config, int fat_blocks) {
  if (block_size_config < 0 || block_size_config > 4 || fat_blocks < 1 ||
      fat_blocks > 32) {
    // Invalid block size configuration
    return -1;
  }

  int actual_block_size = 256 << block_size_config;
  int total_fat_entries = (fat_blocks * actual_block_size) / 2;
  int reserved = 1;  // FAT[0]
  int data_blocks = total_fat_entries - reserved;
  if (fat_blocks == 32 && block_size_config == 4) {
    // this is the special case becase the last block will be 0xffff
    data_blocks = total_fat_entries - reserved - 1;
  }

  // whole system block = fat_blocks (include 2 byte header) + data_blocks

  *fat = malloc(sizeof(fat_st));
  if (!(*fat)) {
    // Allocation failed
    return -1;
  }

  (*fat)->header.block_size = block_size_config;
  (*fat)->header.block_count = fat_blocks;

  (*fat)->entries = malloc(total_fat_entries * sizeof(fat_entry_t));
  if (!(*fat)->entries) {
    free(*fat);
    return -1;
  }

  for (int i = 0; i < total_fat_entries; i++) {
    (*fat)->entries[i].next_block = 0;
  }
  (*fat)->entries[1].next_block = END_OF_CHAIN;

  // total data bytes = data_blocks × actual_block_size
  (*fat)->data_blocks = malloc(data_blocks * actual_block_size);
  if (!(*fat)->data_blocks) {
    free((*fat)->entries);
    free(*fat);
    return -1;
  }
  // Initialize the data blocks to zero
  memset((*fat)->data_blocks, 0, data_blocks * actual_block_size);
  return 0;  // Success
}
// saves the fat table to the by physically writing the the binary file
int k_fat_save(fat_st* fat, const char* path) {
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    return -1;
  }

  int fat_blocks = fat->header.block_count;               // 1~32
  int actual_block_size = 256 << fat->header.block_size;  // 0~4
  int total_fat_entries = (fat_blocks * actual_block_size) / 2;
  int reserved = 1;  // FAT[0] header
  int data_blocks = total_fat_entries - reserved;
  if (fat_blocks == 32 && fat->header.block_size == 4) {
    data_blocks = total_fat_entries - reserved - 1;
  }

  // --- 1. create FAT region ---
  int fat_area_size = fat_blocks * actual_block_size;
  char* fat_area = malloc(fat_area_size);
  if (!fat_area) {
    fclose(fp);
    return -1;
  }

  memset(fat_area, 0, fat_area_size);
  // set FAT header (FAT[0])
  memcpy(fat_area, &fat->header, sizeof(fat_header_t));
  // copy rest of FAT entries from `fat->entries` to `fat_area`
  memcpy(fat_area + sizeof(fat_header_t), &fat->entries[1],
         (total_fat_entries - 1) * sizeof(fat_entry_t));

  if (fwrite(fat_area, actual_block_size, fat_blocks, fp) !=
      (size_t)fat_blocks) {
    free(fat_area);
    fclose(fp);
    return -1;
  }
  free(fat_area);

  // --- 2. Data block ---
  if (fwrite(fat->data_blocks, actual_block_size, data_blocks, fp) !=
      (size_t)data_blocks) {
    fclose(fp);
    return -1;
  }

  fclose(fp);
  return 0;
}

// k_fat_unmount: Unmount the file system (save and free memory).
int k_fat_unmount(fat_st* fat, const char* path) {
  int ret = k_fat_save(fat, path);
  if (ret == 0) {
    free(fat->data_blocks);
    free(fat->entries);
    free(fat);
  }
  return ret;
}
// k_fat_mount: Mount the file system (load from disk).
// This function reads the FAT table from a file and initializes the fat_st
// structure with the FAT entries and data blocks.
// The function reads the FAT header, allocates memory for the FAT entries and
// data blocks, and populates the fat_st structure with the loaded data.
int k_fat_mount(fat_st** fat, const char* path) {
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    return -1;
  }

  *fat = malloc(sizeof(fat_st));
  if (!(*fat)) {
    fclose(fp);
    return -1;
  }

  // read the header part of the FAT table (FAT[0])
  fat_header_t header;
  if (fread(&header, sizeof(fat_header_t), 1, fp) != 1) {
    fclose(fp);
    free(*fat);
    return -1;
  }
  (*fat)->header = header;
  int fat_blocks = header.block_count;
  int actual_block_size = 256 << header.block_size;
  int total_fat_entries = (fat_blocks * actual_block_size) / 2;
  int reserved = 1;
  int data_blocks = total_fat_entries - reserved;
  if (fat_blocks == 32 && header.block_size == 4)
    data_blocks = total_fat_entries - reserved - 1;

  // reset the directory entries count to 0
  fseek(fp, 0, SEEK_SET);

  // get the whole FAT block size (FAT region)
  char* fat_block = malloc(fat_blocks * actual_block_size);
  if (!fat_block) {
    fclose(fp);
    free(*fat);
    return -1;
  }
  if (fread(fat_block, actual_block_size, fat_blocks, fp) !=
      (size_t)fat_blocks) {
    free(fat_block);
    fclose(fp);
    free(*fat);
    return -1;
  }

  // set FAT entries in the fat structure
  (*fat)->entries = malloc(total_fat_entries * sizeof(fat_entry_t));
  if (!(*fat)->entries) {
    free(fat_block);
    fclose(fp);
    free(*fat);
    return -1;
  }
  // FAT[0] put the first 2 bytes (FAT[0])
  memcpy(&((*fat)->entries[0]), fat_block, sizeof(fat_entry_t));
  // copy the rest of the FAT entries from the fat_block
  memcpy(&((*fat)->entries[1]), fat_block + sizeof(fat_header_t),
         (total_fat_entries - 1) * sizeof(fat_entry_t));
  free(fat_block);

  // Data Region:
  (*fat)->data_blocks = malloc(data_blocks * actual_block_size);
  if (!(*fat)->data_blocks) {
    free((*fat)->entries);
    free(*fat);
    fclose(fp);
    return -1;
  }
  if (fread((*fat)->data_blocks, actual_block_size, data_blocks, fp) !=
      (size_t)data_blocks) {
    free((*fat)->data_blocks);
    free((*fat)->entries);
    free(*fat);
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

/*==========================================================================
   FAT Block Manipulation Functions
  ==========================================================================*/
// Allocate a block in the FAT table.
// This function finds the first free block in the FAT table and marks it as
// allocated by setting its next_block field to END_OF_CHAIN. It returns the
int k_fat_allocate_block(fat_st* fat) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries = (fat->header.block_count * actual_block_size) / 2;
  for (int i = 1; i < total_entries; i++) {
    if (fat->entries[i].next_block == 0) {
      fat->entries[i].next_block = END_OF_CHAIN;
      // allocating the ith block by setting everything inside it to 0
      // do not need to wipe out all the previous data
      // memset(block_address(fat, actual_block_size, i), 0, actual_block_size);
      return i;
    }
  }
  return -1;
}
// Check if there are any free blocks available in the FAT table.
// This function iterates through the FAT entries and checks if there are any
// blocks with next_block set to 0. If a free block is found, it returns
// AVALIABLE_FREE_BLOCKS. If no free blocks are found, it returns
// NO_FREE_BLOCKS.
int k_fat_able_to_allocate_block(fat_st* fat) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries = (fat->header.block_count * actual_block_size) / 2;
  for (int i = 1; i < total_entries; i++) {
    if (fat->entries[i].next_block == 0) {
      return AVALIABLE_FREE_BLOCKS;  // able to allocate
    }
  }
  return NO_FREE_BLOCKS;  // no free block (-1)
}

// Free a block in the FAT table.
// This function marks a block as free by setting its next_block field to 0.
int k_fat_free_block(fat_st* fat, block_num_t block_num) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries = (fat->header.block_count * actual_block_size) / 2;
  if (block_num <= 0 || block_num >= total_entries) {
    return -1;
  }

  fat->entries[block_num].next_block = 0;
  return 0;
}

// Write data to a block in the FAT table.
// This function writes num_bytes of data from buf to the specified block_num
int k_fat_write(fat_st* fat,
                block_num_t block_num,
                const char* buf,
                int num_bytes,
                int offset) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries =
      (fat->header.block_count * (256 << fat->header.block_size)) / 2;
  if (block_num < 1 || block_num >= total_entries)
    return -1;
  char* dest = block_address(fat, actual_block_size, block_num);
  if (offset + num_bytes > actual_block_size) {
    num_bytes = actual_block_size - offset;
  }

  memcpy(dest + offset, buf, num_bytes);
  return num_bytes;
}

// Read data from a block in the FAT table.
// This function reads num_bytes of data from the specified block_num into buf.
// It returns the number of bytes read.
int k_fat_read(fat_st* fat, block_num_t block_num, char* buf, int num_bytes) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries =
      (fat->header.block_count * (256 << fat->header.block_size)) / 2;
  if (block_num < 1 || block_num >= total_entries)
    return -1;
  char* src = block_address(fat, actual_block_size, block_num);
  if (num_bytes > actual_block_size)
    num_bytes = actual_block_size;
  memcpy(buf, src, num_bytes);
  return num_bytes;
}

// Find a file in the FAT table.
int k_fat_find(fat_st* fat, const char* path) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      return g_directory_entries[i].first_block;
    }
  }
  return K_FAT_FIND_FILE_NO_EXIST;
}

// Extend a FAT chain by allocating one more block.
// This function allocates a new block and links it to the last block in the
block_num_t k_fat_extend_one_block(fat_st* fat, block_num_t last_block_num) {
  block_num_t new_block = k_fat_allocate_block(fat);
  if (new_block < 0)
    return -1;
  fat->entries[last_block_num].next_block = new_block;
  fat->entries[new_block].next_block = END_OF_CHAIN;
  return new_block;
}

// Add a block to a FAT chain.
// This function adds a new block to the FAT chain by linking it to the
int k_fat_add_block(fat_st* fat,
                    block_num_t block_num,
                    block_num_t new_block_num) {
  int actual_block_size = 256 << fat->header.block_size;
  int total_entries = (fat->header.block_count * actual_block_size) / 2;
  if (block_num < 1 || new_block_num < 1 || block_num >= total_entries ||
      new_block_num >= total_entries)
    return -1;
  int temp = fat->entries[block_num].next_block;
  fat->entries[block_num].next_block = new_block_num;
  fat->entries[new_block_num].next_block = temp;
  return 0;
}

// Get the next block in a FAT chain.
// This function retrieves the next block number in the FAT chain for the
// specified block_num. It returns the next block number or -1 if the block_num
// is invalid. If the block_num is END_OF_CHAIN, it returns END_OF_CHAIN.
block_num_t k_fat_get_next_block(fat_st* fat, block_num_t block_num) {
  int total_entries =
      (fat->header.block_count * (256 << fat->header.block_size)) / 2;
  if (block_num < 1 || block_num >= total_entries)
    return -1;
  return fat->entries[block_num].next_block;
}

// Free a FAT chain.
// This function frees a FAT chain starting from the specified block_num.
// It iterates through the chain, freeing each block until it reaches
// END_OF_CHAIN or 0. It returns 0 on success.
int k_fat_free_chain(fat_st* fat, block_num_t block_num) {
  while (block_num != END_OF_CHAIN && block_num != 0) {
    block_num_t next = fat->entries[block_num].next_block;
    k_fat_free_block(fat, block_num);
    block_num = next;
  }
  return 0;
}

/*==========================================================================
   File and Directory Functions
  ==========================================================================*/

// loads a certain file from the fat and returns a file control block
file_control_block_t* k_get_file(fat_st* fat, const char* path) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      file_control_block_t* fcb =
          (file_control_block_t*)malloc(sizeof(file_control_block_t));
      if (!fcb)
        return NULL;
      strncpy(fcb->name, g_directory_entries[i].name, sizeof(fcb->name));
      fcb->size = g_directory_entries[i].size;
      fcb->first_block = g_directory_entries[i].first_block;
      fcb->type = g_directory_entries[i].type;
      fcb->perm = g_directory_entries[i].perm;
      fcb->mtime = g_directory_entries[i].mtime;
      fcb->ref_count = 1;
      fcb->offset = 0;
      fcb->synced = 1;
      return fcb;
    }
  }
  return NULL;
}

// Create a new file in the FAT table.
// This function checks if the file name is valid, if the directory is not full,
// and if there is space to create a new block. If all conditions are met, it
// creates a new file entry in the global directory entries array and updates
// the root directory in the FAT table. It returns 0 on success
int k_create_file(fat_st* fat,
                  const char* file_name,
                  uint8_t type,
                  uint8_t perm) {
  if (!k_is_valid_posix_filename(file_name)) {
    s_printf(STDERR_FILENO,
             "Error: Invalid filename '%s'. Must match POSIX portable filename "
             "character set.\n",
             file_name);
    return K_CREATE_FILE_ERROR;
  }
  if (g_directory_count >= MAX_FILES) {
    s_printf(STDERR_FILENO, "Error: Directory is full\n");
    return K_CREATE_FILE_ERROR;
  }
  // s_printf(STDERR_FILENO, "creating new file %s\n", file_name);
  // check if the file already exists
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, file_name) == 0)
      return K_CREATE_FILE_ERROR;
  }
  // check if there are avaliable space to create an additional block
  if (k_fat_able_to_allocate_block(fat) == NO_FREE_BLOCKS) {
    s_printf(STDERR_FILENO, "Error: No free blocks available\n");
    return K_CREATE_FILE_ERROR;
  }

  // we don't allocate FAT block for the first block variable yet, we
  // simply set it to be constant end_of_chain so it can be allocated later
  //  type must be 0 (regular file) or 1 (directory)
  block_num_t first_block = END_OF_CHAIN;

  if (type == 0 && first_block < 0)
    return K_CREATE_FILE_ERROR;

  directory_entry_t new_entry;
  memset(&new_entry, 0, sizeof(new_entry));
  strncpy(new_entry.name, file_name,
          sizeof(new_entry.name) - 1);  // copy the file name
  new_entry.size = 0;
  new_entry.first_block = first_block;
  new_entry.type = type;
  new_entry.perm = perm;
  new_entry.mtime = time(NULL);

  // add new entry into the global directory entries array
  g_directory_entries[g_directory_count++] = new_entry;

  // need to write global entries to the bin file
  k_update_root_directory(fat);

  // printf("saving to %s\n", fs_name);
  k_fat_save(fat, fs_name);

  return 0;
}

// Check if a filename is valid according to POSIX portable filename character
// set. The valid characters are A-Z, a-z, 0-9, '.', '_', and '-'.
// This function returns true if the filename is valid, false otherwise.
bool k_is_valid_posix_filename(const char* name) {
  for (int i = 0; name[i] != '\0'; i++) {
    char c = name[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || (c == '.') || (c == '_') || (c == '-'))) {
      return false;
    }
  }
  return true;
}

// assune directory_entry_t is a foixed structure
void k_update_root_directory(fat_st* fat) {
  int actual_block_size = 256 << fat->header.block_size;
  char* root_dir_ptr = block_address(fat, actual_block_size, 1);
  memset(root_dir_ptr, 0, actual_block_size);

  // calculate the number of entries that can fit in a block
  int entry_size = sizeof(directory_entry_t);
  int max_entries = actual_block_size / entry_size;  // max entries per block

  // we need to expand the root directory if max entries for the root directory
  // is not enough
  // int count = (g_directory_count < max_entries) ? g_directory_count :
  // max_entries;
  int remaining = g_directory_count;
  block_num_t current_block = 1;  // root directory starts at block 1
  int written = 0;
  while (remaining > 0) {
    int to_write = (remaining < max_entries) ? remaining : max_entries;
    char* curr_block_addr_ptr =
        block_address(fat, actual_block_size, current_block);
    memcpy(curr_block_addr_ptr, &g_directory_entries[written],
           to_write * entry_size);

    written += to_write;
    remaining -= to_write;

    // Check if we need to allocate a new block for the next write
    if (remaining > 0) {
      block_num_t next = k_fat_get_next_block(fat, current_block);

      if (next == END_OF_CHAIN) {
        // Allocate a new block for the next write
        next = k_fat_extend_one_block(fat, current_block);
        if (next < 0) {
          s_printf(STDERR_FILENO,
                   "Error: Unable to allocate new block for root directory!\n");
          return;
        }
      }
      current_block = next;
    }
  }
}

// Clear the data blocks of a directory.
// This function iterates through the data blocks of a directory and clears
// their contents by setting them to zero. It starts from the first block and
// continues until it reaches END_OF_CHAIN.
void k_clear_directory_data_blocks(fat_st* fat) {
  int actual_block_size = 256 << fat->header.block_size;
  block_num_t current_block = 1;

  while (current_block != END_OF_CHAIN) {
    char* block_ptr = block_address(fat, actual_block_size, current_block);
    memset(block_ptr, 0, actual_block_size);

    current_block = k_fat_get_next_block(fat, current_block);
  }
}

// Remove a file from the FAT table.
// This function searches for the file in the global directory entries array.
// If the file is found, it frees the associated data blocks and shifts the
// directory entries to overwrite the removed file.
int k_remove_file(fat_st* fat, const char* path) {
  int found = -1;
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      found = i;
      break;
    }
  }
  if (found == -1) {
    s_printf(STDERR_FILENO, "Error: File %s not found\n", path);
    return -1;
  }
  // if type == 0, then it is a regular file, they == 1, then it is a directory
  if (g_directory_entries[found].type == 0 &&
      g_directory_entries[found].first_block > 0) {
    k_fat_free_chain(fat, g_directory_entries[found].first_block);
  }
  // shifts the directory to overwrite the existing directory
  for (int i = found; i < g_directory_count - 1; i++) {
    g_directory_entries[i] = g_directory_entries[i + 1];
  }
  g_directory_count--;
  // Optional: clear the now-unused last entry
  memset(&g_directory_entries[g_directory_count], 0, sizeof(directory_entry_t));

  // zeros out the directory entry in the data region
  k_clear_directory_data_blocks(fat);

  // update the root directory
  k_update_root_directory(fat);
  // save the FAT table to the file
  k_fat_save(fat, fs_name);
  return 0;
}

// Update the size of a file in the FAT table.
// This function searches for the file in the global directory entries array.
// If the file is found, it updates its size field and returns 0. If the file
// is not found, it returns -1.
int k_update_file_size(fat_st* fat, const char* path, uint32_t size) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      g_directory_entries[i].size = size;
      return 0;
    }
  }
  return -1;
}

// Update the modification time of a file in the FAT table.
// This function searches for the file in the global directory entries array.
// If the file is found, it updates its modification time field and returns 0.
// If the file is not found, it returns -1.
int k_update_file_mtime(fat_st* fat, const char* path, time_t mtime) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      g_directory_entries[i].mtime = mtime;
      return 0;
    }
  }
  return -1;
}

// Update the name of a file in the FAT table.
// This function searches for the file in the global directory entries array.
// If the file is found, it updates its name field and returns 0. If the file
// is not found, it returns -1. The new name must be a valid POSIX filename.
int k_update_file_name(fat_st* fat, const char* path, const char* new_path) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      // Safely copy up to sizeof(name)-1 chars, then force null-termination
      strncpy(g_directory_entries[i].name, new_path,
              sizeof(g_directory_entries[i].name));
      g_directory_entries[i].name[sizeof(g_directory_entries[i].name) - 1] =
          '\0';
      // Persist the change
      k_update_root_directory(fat);
      k_fat_save(fat, fs_name);
      return 0;
    }
  }
  return -1;
}

/*==========================================================================
   File Descriptor and I/O Functions
  ==========================================================================*/
// Get the file descriptor table index for a given process ID.
// opens the file by iterating through the global file table to find an empty
// entry and assigning the file control block (fcb) to it. It also iterates
// through the process file descriptor table to find an empty slot and stores
// the reference to the file index in the global file table. The function
// returns the file descriptor (fd) for the opened file or -1 if an error
// occurs.
int k_open(fat_st* fat, const char* file_name, int mode) {
  file_control_block_t* fcb = k_get_file(fat, file_name);
  if (!fcb) {
    return K_OPEN_FILE_DOES_NOT_EXIST;
  }
  // iterates through the global file table to find an empty entry
  // once empty slot has been found, assign the fcb to it and returns the index
  // basically the process of "opening" a file
  int file_index = -1;
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (!global_file_table.entries[i].in_use) {
      global_file_table.entries[i].fcb = fcb;
      global_file_table.entries[i].in_use = 1;
      file_index = i;
      global_file_table.open_file_count++;
      break;
    }
  }
  if (file_index == -1) {
    free(fcb);
    return -1;
  }
  // iterates through the process fd table
  int fd = -1;
  for (int i = 3; i < MAX_FDS_PER_PROCESS; i++) {
    // find an empty slot in the process fd table, stores reference to the
    // file_index in the global file table
    pid_t curr_proc_pid = s_getpid();
    int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
    proc_table_entry_t* proc_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
    if (proc_entry->fd_table.entries[i].file_index == -1) {
      proc_entry->fd_table.entries[i].file_index = file_index;
      proc_entry->fd_table.entries[i].cursor = 0;
      proc_entry->fd_table.open_file_count++;
      fd = i;  // fd is the file descriptor within the process fd table, aka the
               // index the file is stored within
      break;
    }
  }
  if (fd == -1) {
    global_file_table.entries[file_index].in_use = 0;
    free(fcb);
    return -1;
  }
  // @TODO: kernal people - check for only one process trying to write to the
  // file at a time if mode is F_WRITE
  if (mode == F_WRITE) {
    // create the file if the file does not exist, aka no data and is only just
    // a directory in this case, the first block is END_OF_CHAIN doesn't k_write
    // already does this??
    // s_printf(STDERR_FILENO, "first block is %hu\n", fcb->first_block);
    if (fcb->first_block == END_OF_CHAIN) {
      // allocate a new block for the file
      block_num_t new_block = k_fat_allocate_block(fat);
      if (new_block < 0) {
        global_file_table.entries[file_index].in_use = 0;
        free(fcb);
        return -1;
      }
      fcb->first_block = new_block;
      fcb->size = 0;
      fcb->mtime = time(NULL);
      // update the global directory entries after allocation
      for (int i = 0; i < g_directory_count; i++) {
        if (strcmp(g_directory_entries[i].name, file_name) == 0) {
          g_directory_entries[i].first_block = new_block;
          break;
        }
      }
      // update the process fd table
      pid_t curr_proc_pid = s_getpid();
      int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
      proc_table_entry_t* proc_entry =
          (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
      proc_entry->fd_table.entries[fd].file_index = file_index;
      proc_entry->fd_table.entries[fd].cursor = 0;
      // proc_fd_table.open_file_count++; double check this
      k_update_root_directory(fat);
      k_fat_save(fat, fs_name);
      // s_printf(STDERR_FILENO, "creating file %s, first block# %u\n",
      // file_name,
      //         fcb->first_block);
    }

    // truncate the file if the file exists, aka data actually written to the
    // blocks in this case, the first block's next is not END_OF_CHAIN, so
    // truncating to only the first block
    else if (fat->entries[fcb->first_block].next_block != END_OF_CHAIN) {
      // we want to keep the first block and truncate the rest
      block_num_t next_after_first = fat->entries[fcb->first_block].next_block;
      fat->entries[fcb->first_block].next_block = END_OF_CHAIN;
      // s_printf(STDERR_FILENO, "freeing block# %hu\n", next_after_first);
      k_fat_free_chain(fat, next_after_first);

      // fcb->first_block = END_OF_CHAIN;
      fcb->size = 0;
      fcb->mtime = time(NULL);
      pid_t curr_proc_pid = s_getpid();
      int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
      proc_table_entry_t* proc_entry =
          (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
      proc_entry->fd_table.entries[fd].cursor = 0;

      // update the global directory entries after truncation
      for (int i = 0; i < g_directory_count; i++) {
        if (strcmp(g_directory_entries[i].name, file_name) == 0) {
          // do not modify the first block, already freed the subsequent blocks
          // after the first block
          g_directory_entries[i].size = 0;
          break;
        }
      }
      k_update_root_directory(fat);
      k_fat_save(fat, fs_name);

      // writing to it
    } else {
      fcb->size = 0;
      fcb->mtime = time(NULL);
      pid_t curr_proc_pid = s_getpid();
      int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
      proc_table_entry_t* proc_entry =
          (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
      proc_entry->fd_table.entries[fd].cursor = 0;
      // update the global directory entries after truncation
      for (int i = 0; i < g_directory_count; i++) {
        if (strcmp(g_directory_entries[i].name, file_name) == 0) {
          g_directory_entries[i].size = 0;
          break;
        }
      }
      // s_printf(STDERR_FILENO, "first block is %hu\n", fcb->first_block);
    }

  }

  else if (mode == F_APPEND) {
    // if the file is opened in append mode, set the cursor to the end of the
    // file
    pid_t curr_proc_pid = s_getpid();
    int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
    proc_table_entry_t* proc_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
    proc_entry->fd_table.entries[fd].cursor = fcb->size;
  }
  // @todo: kernal people - make sure each process has its own cursor and reads
  // starting from that cursor
  else if (mode == F_READ) {
    // if the file is opened in read mode, update the process's cursor to be the
    // default offset
    pid_t curr_proc_pid = s_getpid();
    int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
    proc_table_entry_t* proc_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
    proc_entry->fd_table.entries[fd].cursor = fcb->size;
    proc_entry->fd_table.entries[fd].cursor = fcb->offset;
  } else {
    // invalid mode
    global_file_table.entries[file_index].in_use = 0;
    free(fcb);
    return -1;
  }

  return fd;
}

// read into the buffer n bytes from the file descriptor fd
// This function reads data from the file associated with the file descriptor
// fd and stores it in the buffer buf. It reads up to n bytes of data and
// returns the number of bytes read.
int k_read(fat_st* fat, int fd, int n, char* buf) {
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS) {
    return -1;
  }

  pid_t curr_proc_pid = s_getpid();
  int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
  proc_table_entry_t* tbl_entry =
      (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);

  if (tbl_entry == NULL) {
    return -1;
  }
  int tbl = tbl_entry->fd_table.entries[fd].file_index;

  if (tbl == -2 && fd < 3) {
    return read(fd, buf, n);
  }

  int file_index = tbl_entry->fd_table.entries[fd].file_index;
  if (file_index < 0 || file_index >= MAX_OPEN_FILES)
    return -1;
  file_control_block_t* fcb = global_file_table.entries[file_index].fcb;
  if (!fcb)
    return -1;

  int actual_block_size = 256 << fat->header.block_size;
  int cursor = tbl_entry->fd_table.entries[fd].cursor;
  if (cursor >= (int)fcb->size)
    return 0;

  int total_read = 0;
  while (n > 0 && cursor < (int)fcb->size) {
    int block_offset = cursor / actual_block_size;
    int intra_offset = cursor % actual_block_size;

    // find the correct block based on the cursor
    block_num_t current_block = fcb->first_block;
    for (int i = 0; i < block_offset; i++) {
      current_block = k_fat_get_next_block(fat, current_block);
      if (current_block == END_OF_CHAIN)
        break;
    }
    if (current_block == END_OF_CHAIN)
      break;

    char temp_block[actual_block_size];
    if (k_fat_read(fat, current_block, temp_block, actual_block_size) < 0)
      return -1;

    // calculate how many bytes we can read from the current block
    int available = actual_block_size - intra_offset;
    int remaining_file = fcb->size - cursor;
    int to_read = n;
    if (to_read > available)
      to_read = available;
    if (to_read > remaining_file)
      to_read = remaining_file;

    // copy the data from the temp_block to the buf
    memcpy(buf + total_read, temp_block + intra_offset, to_read);

    total_read += to_read;
    cursor += to_read;
    n -= to_read;
    proc_table_entry_t* proc_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
    if (proc_entry == NULL) {
      return -1;
    }
    proc_entry->fd_table.entries[fd].cursor = cursor;
  }
  return total_read;
}

// write n bytes from the buffer str to the file descriptor fd
// This function writes data from the buffer str to the file associated with
// the file descriptor fd. It writes up to n bytes of data and returns the
// number of bytes written.
int k_write(fat_st* fat, int fd, const char* str, int n) {
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -1;

  pid_t curr_proc_pid = s_getpid();
  int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
  proc_table_entry_t* proc_entry =
      (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
  int tbl = proc_entry->fd_table.entries[fd].file_index;

  if (tbl == -2 && fd < 3) {
    return write(fd, str, n);
  }

  // ...【original function】...
  // proc_table_entry_t* proc_entry =
  //     (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
  int file_index = proc_entry->fd_table.entries[fd].file_index;
  if (file_index < 0 || file_index >= MAX_OPEN_FILES)
    return -1;
  file_control_block_t* fcb = global_file_table.entries[file_index].fcb;
  if (!fcb)
    return -1;

  int actual_block_size = 256 << fat->header.block_size;
  int total_written = 0;
  int remaining = n;

  while (remaining > 0) {
    int cursor = proc_entry->fd_table.entries[fd].cursor;
    int block_offset = cursor / actual_block_size;
    int intra_offset = cursor % actual_block_size;

    block_num_t current_block = fcb->first_block;
    // if first block hasn't been allocated yet, allocate it now
    if (current_block == END_OF_CHAIN) {
      current_block = k_fat_allocate_block(fat);
      if (current_block < 0)
        return -1;
      fcb->first_block = current_block;
      fcb->size = 0;
      //----- updates the global directory table
      // get the name/path of the current file
      char path[32];
      strncpy(path, fcb->name, sizeof(path));
      path[sizeof(path) - 1] = '\0';  // ensure null-termination

      // update global directory entries after allocation
      for (int i = 0; i < g_directory_count; i++) {
        if (strcmp(g_directory_entries[i].name, path) == 0) {
          g_directory_entries[i].first_block = current_block;
          break;
        }
      }
    }

    for (int i = 0; i < block_offset; i++) {
      block_num_t next = k_fat_get_next_block(fat, current_block);
      if (next == END_OF_CHAIN) {
        // allocate a new block for the next write
        next = k_fat_extend_one_block(fat, current_block);
        if (next < 0)
          return -1;
      }
      current_block = next;
    }

    int available = actual_block_size - intra_offset;
    int to_write = (remaining < available) ? remaining : available;

    int written = k_fat_write(fat, current_block, str + total_written, to_write,
                              intra_offset);
    if (written < 0)
      return -1;
    total_written += written;
    remaining -= written;
    proc_entry->fd_table.entries[fd].cursor += written;
    fcb->size += written;
  }

  fcb->mtime = time(NULL);
  k_update_file_size(fat, fcb->name, fcb->size);
  k_update_file_mtime(fat, fcb->name, fcb->mtime);
  return total_written;
}

// Close a file descriptor.
// This function closes the file descriptor fd by decrementing the reference
// count of the associated file control block (fcb). If the reference count
// reaches zero, it frees the fcb and marks the file entry in the global file as
// unused
int k_close(fat_st* fat, int fd) {
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS) {
    return -1;
  }

  pid_t curr_proc_pid = s_getpid();
  int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
  proc_table_entry_t* proc_entry =
      (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);

  if (proc_entry->fd_table.entries[fd].file_index == -2) {
    return 0;
  }

  int file_index = proc_entry->fd_table.entries[fd].file_index;
  if (file_index < 0)
    return -1;
  file_control_block_t* fcb = global_file_table.entries[file_index].fcb;
  if (fcb) {
    fcb->ref_count--;
    if (fcb->ref_count <= 0) {
      free(fcb);
      global_file_table.entries[file_index].fcb = NULL;
      global_file_table.entries[file_index].in_use = 0;
      global_file_table.open_file_count--;
    }
  }
  proc_entry->fd_table.entries[fd].file_index = -1;
  proc_entry->fd_table.entries[fd].cursor = 0;
  return 0;
}

// Unlink a file from the FAT table.
// This function removes the file from the global directory entries array
// and frees the associated data blocks. It also updates the root directory
// and saves the FAT table to the file. It returns 0 on success or -1 on
// failure. The function first checks if the file is in use by any process.
int k_unlink(fat_st* fat, const char* path) {
  // 1) if some file is using, then it sould show error
  // check if any entries is using the file
  // for (int i = 0; i < MAX_OPEN_FILES; i++) {
  //   if (global_file_table.entries[i].in_use &&
  //       strcmp(global_file_table.entries[i].fcb->name, path) == 0) {
  //     errno = EBUSY;  // “Device or resource busy”
  //     return -1;
  //   }
  // }
  // 2) if no file is using, then can delete
  return k_remove_file(fat, path);
}

// Seek to a specific position in a file.
// This function moves the file cursor to a specified position in the file
// associated with the file descriptor fd. It takes an offset and a
// whence parameter to determine the new position.
int k_lseek(fat_st* fat, int fd, int offset, int whence) {
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS) {
    return -1;
  }

  if (fd < 3) {
    // fd =  0,1,2 system lseek
    return lseek(fd, offset, whence);
  }
  pid_t curr_proc_pid = s_getpid();
  int table_index = k_get_fd_table_index_by_pid(curr_proc_pid);
  proc_table_entry_t* proc_entry =
      (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
  int file_index = proc_entry->fd_table.entries[fd].file_index;

  if (file_index < 0)
    return -1;
  file_control_block_t* fcb = global_file_table.entries[file_index].fcb;
  if (!fcb)
    return -1;
  int new_cursor;
  switch (whence) {
    case F_SEEK_SET:  // SEEK_SET
      new_cursor = offset;
      break;
    case F_SEEK_CUR:  // SEEK_CUR
      new_cursor = proc_entry->fd_table.entries[fd].cursor + offset;
      break;
    case F_SEEK_END:  // SEEK_END
      new_cursor = fcb->size + offset;
      break;
    default:
      return -1;
  }
  if (new_cursor < 0 || new_cursor > (int)fcb->size)
    return -1;
  proc_entry->fd_table.entries[fd].cursor = new_cursor;
  return new_cursor;
}

// List the files in the directory.
int k_ls(void) {
  // 1) gather pointers to valid entries
  size_t cap = g_directory_count;
  directory_entry_t** list = malloc(cap * sizeof(*list));
  if (!list)
    return -1;

  size_t n = 0;
  for (int i = 0; i < g_directory_count; i++) {
    list[n++] = &g_directory_entries[i];
  }

  // 2) mark none printed yet
  char* used = calloc(n, 1);
  if (!used) {
    free(list);
    return -1;
  }

  // 3) selection‑sort print
  for (size_t it = 0; it < n; it++) {
    size_t best = (size_t)-1;
    for (size_t j = 0; j < n; j++) {
      if (used[j])
        continue;
      if (best == (size_t)-1 ||
          list[j]->first_block < list[best]->first_block) {
        best = j;
      }
    }
    used[best] = 1;
    directory_entry_t* e = list[best];

    // build permission string
    char perm_str[5];
    perm_str[0] = (e->type == 1) ? 'd' : '-';
    perm_str[1] = (e->perm & 4) ? 'r' : '-';
    perm_str[2] = (e->perm & 2) ? 'w' : '-';
    perm_str[3] = (e->perm & 1) ? 'x' : '-';
    perm_str[4] = '\0';

    // format time
    char timebuf[64];
    struct tm tm_info;
    localtime_r(&e->mtime, &tm_info);
    strftime(timebuf, sizeof timebuf, "%b %e %H:%M:%S %Y", &tm_info);

    // prepare block display: blank for 65535, else right‑aligned
    char blockbuf[6];
    if (e->first_block == 65535) {
      memset(blockbuf, ' ', 5);
      blockbuf[5] = '\0';
    } else {
      snprintf(blockbuf, sizeof blockbuf, "%5u", e->first_block);
    }

    s_printf(STDERR_FILENO, "%5s %-2s %5u %s %s\n", blockbuf, perm_str, e->size,
             timebuf, e->name);
  }

  free(used);
  free(list);
  return 0;
}

// Clear the global directory entries array.
void k_clear_directory_entries(void) {
  g_directory_count = 0;
}

// read the root directory and update g_directory_entries
void k_load_directory_entries(fat_st* fat) {
  int actual_block_size = 256 << fat->header.block_size;
  // root directory is in data region  block 1
  int entry_size = sizeof(directory_entry_t);
  int max_entries = actual_block_size / entry_size;
  g_directory_count = 0;
  block_num_t curr_block_num = 1;  // root block number

  while (curr_block_num != END_OF_CHAIN) {
    char* curr_block_addr_ptr =
        block_address(fat, actual_block_size, curr_block_num);
    directory_entry_t* entries = (directory_entry_t*)curr_block_addr_ptr;
    for (int i = 0; i < max_entries; i++) {
      // if the first byte is '\0'，then there is no more entries
      if (entries[i].name[0] == '\0')
        break;
      g_directory_entries[g_directory_count++] = entries[i];
    }
    // checks for more than one block of directory
    block_num_t next = k_fat_get_next_block(fat, curr_block_num);
    curr_block_num = next;
  }
}

// Get the file permission of a file in the FAT table.
int k_file_permission(fat_st* fat, const char* path) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      return g_directory_entries[i].perm;
    }
  }
  // File not found
  s_printf(STDERR_FILENO, "Error: File %s not found\n", path);
  return -1;
}

// Read a line from a file descriptor.
// This function reads a line of text from the file descriptor fd and stores
// it in the buffer buf. It reads up to maxlen - 1 characters and appends a
// null terminator. It returns the number of characters read, or -1 on error.
ssize_t k_read_line(int fd, char* buf, size_t maxlen) {
  size_t n = 0;
  char c;
  ssize_t rc;

  while (n < maxlen - 1) {
    rc = read(fd, &c, 1);
    if (rc == 1) {
      buf[n++] = c;
      if (c == '\n')
        break;  // stop at newline
    } else if (rc == 0) {
      // EOF
      break;
    } else {
      // rc < 0
      if (errno == EINTR)
        continue;  // interrupted, retry
      return -1;   // actual error
    }
  }

  if (n == 0 && rc == 0)
    return 0;  // EOF and no data

  buf[n] = '\0';
  return n;
}

/**
 * @brief update file perm（8-bit）
 * @return 0 success，-1 no file is found
 */
int k_update_file_perm(fat_st* fat, const char* path, uint8_t perm) {
  for (int i = 0; i < g_directory_count; i++) {
    if (strcmp(g_directory_entries[i].name, path) == 0) {
      g_directory_entries[i].perm = perm;
      return 0;
    }
  }
  return -1;
}

// our own dup2
int k_dup2(fat_st* fat,
           int oldfd,
           int newfd,
           pid_t pid,
           const char* argv) {  // STDIN input_fd  1 , 3
  terminal_logger_debug("k_dup2: oldfd:%d newfd:%d\n", oldfd, newfd);
  // 1. check
  if (oldfd < 0 || oldfd >= MAX_FDS_PER_PROCESS || newfd < 0 ||
      newfd >= MAX_FDS_PER_PROCESS) {
    terminal_logger_debug("k_dup2: invalid fd\n");
    return -1;
  }
  // 2. oldfd must exist
  // check for reserved case for case 0, 1, 2

  // if (old_index < 0) {
  //   return -1;
  // }
  int table_index = k_get_fd_table_index_by_pid(pid);
  if (table_index < 0) {
    terminal_logger_debug("k_dup2: invalid table index\n");
    return -1;
  } else {
    terminal_logger_debug("k_dup2: table index %zu\n", table_index);
  }
  proc_table_entry_t* proc_entry =
      (proc_table_entry_t*)vec_get(&proc_fd_table, table_index);
  if (proc_entry->fd_table.entries[oldfd].file_index == -1) {
    dprintf(STDERR_FILENO, "k_dup2: oldfd %d not exist, will need to create \n",
            oldfd);
    return -1;
  }

  if (oldfd == newfd) {
    terminal_logger_debug("k_dup2: oldfd == newfd\n");
    return newfd;
  }

  // 5. copy the entries
  // proc_fd_table.entries[newfd] = proc_fd_table.entries[oldfd];
  proc_entry->fd_table.entries[oldfd] = proc_entry->fd_table.entries[newfd];
  // 6. count++
  // check if newfd is 0 - 2(reserved for stdin, stdout, stderr)
  file_control_block_t* fcb = NULL;
  if (newfd >= 3) {
    fcb = global_file_table.entries[newfd - 3].fcb;
  }
  // file_control_block_t* fcb = proc_fd_table.entries[old_index].fcb;
  if (fcb) {
    fcb->ref_count++;
  }
  // 4. close newfd
  if (proc_entry->fd_table.entries[newfd].file_index >= 0) {
    terminal_logger_debug("k_dup2: close newfd %d for process %d\n", newfd,
                          pid);
    k_close(fat, newfd);
  }
  return newfd;
}
// get the fd table index by pid
// This function iterates through the process file descriptor table to find
int k_get_fd_table_index_by_pid(pid_t pid) {
  int table_index = -1;
  for (int i = 0; i < vec_len(&proc_fd_table); i++) {
    proc_table_entry_t* proc_entry =
        (proc_table_entry_t*)vec_get(&proc_fd_table, i);
    if (proc_entry != NULL && proc_entry->proc_id == pid) {
      table_index = i;
      return table_index;
    }
  }
  return -10;
}