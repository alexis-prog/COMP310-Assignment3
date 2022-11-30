#ifndef SFS_API_H
#define SFS_API_H

#include <stdint.h>

// 
#define MAGIC_NUMBER 0xABCD0005
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 32
#define NUM_FREE_BLOCKS (NUM_BLOCKS / 8 / BLOCK_SIZE + 1)
#define POINTER_SIZE 4

#define BLOCK_CACHE_SIZE 16
#define INODE_CACHE_SIZE 16

#define INODE_SIZE 64
#define INODES_PER_BLOCK (BLOCK_SIZE / INODE_SIZE)
#define INODE_DIRECT_ACCESS 12
#define INODE_MAX_BLOCKS (INODE_DIRECT_ACCESS + BLOCK_SIZE / POINTER_SIZE)

#define DIR_ENTRY_SIZE 64
#define DIR_ENTRIES_PER_BLOCK (BLOCK_SIZE / DIR_ENTRY_SIZE)

#define MAXFILENAME (64 - 5)

#define MAX_OPEN_FILES 16

// Never trust char size
typedef uint8_t byte_t;

// You can add more into this file.

void mksfs(int);

int sfs_getnextfilename(char*);

int sfs_getfilesize(const char*);

int sfs_fopen(char*);

int sfs_fclose(int);

int sfs_fwrite(int, const char*, int);

int sfs_fread(int, char*, int);

int sfs_fseek(int, int);

int sfs_remove(char*);

#endif
