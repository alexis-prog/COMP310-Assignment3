#ifndef SFS_INODE_H
#define SFS_INODE_H

#include "sfs_api.h"
#include "sfs_block.h"
#include "disk_emu.h"

// INODE - 64 bytes -> 16 per block
typedef struct _inode_t {
    uint32_t mode;
    uint32_t link_count;
    uint32_t size;

    uint32_t direct[INODE_DIRECT_ACCESS];
    uint32_t indirect;
} inode_t;

// I-Node management
void init_inode_cache();

void write_inode_to_disk(uint32_t inode_num, inode_t* inode);

uint32_t get_oldest_inode();

void get_inode(uint32_t inode_num, inode_t* inode);

uint32_t get_next_free_inode();

void write_inode(inode_t* node, uint32_t index);

void flush_inode_cache();

int read_from_inode(inode_t* node, uint32_t offset, uint32_t size, void* buffer);

int write_to_inode(uint32_t, inode_t*, uint32_t, byte_t*, uint32_t);

#endif