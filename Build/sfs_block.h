#ifndef SFS_BLOCK_H
#define SFS_BLOCK_H

#include "disk_emu.h"
#include "sfs_api.h"

// Fixed size of 1024 bytes
typedef struct _block_t{
    byte_t data[BLOCK_SIZE];
} block_t;


// Superblock representation (fixed at 1024 byte for ease of use)
typedef struct _superblock_t {
    uint32_t magic;
    uint32_t block_size;
    uint32_t file_system_size;
    uint32_t inode_table_length;
    uint32_t root_dir_inode;
    byte_t padding[BLOCK_SIZE - 5*sizeof(uint32_t)];
} superblock_t;

superblock_t* get_superblock();

// Block cache management
void init_block_cache();

uint32_t get_oldest_block();

void _write_block(uint32_t block_num, block_t* block);

void _read_block(uint32_t block_num, block_t* block);

int is_block_free(uint32_t block_num);

void set_block_status(uint32_t block_num, int status);

uint32_t get_next_free_block();

void flush_block_cache();

#endif