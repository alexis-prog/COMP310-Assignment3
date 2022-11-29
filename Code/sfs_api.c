#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_api.h"
#include "disk_emu.h"

typedef uint8_t byte;

char* disk_name = "disk";
uint32_t magic = 0xABCD0005;
uint32_t block_size = 1024;
uint32_t num_blocks = 1024;


typedef struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t file_system_size;
    uint32_t inode_table_length;
    uint32_t root_dir_inode;
} superblock;

typedef struct inode {
    byte mode;
    byte link_count;
    byte uid;
    byte gid;

    uint32_t direct[12];
    uint32_t indirect;
} inode;

// Initialization helper functions
void write_inode(inode* node, int index){
    byte* buffer = calloc(1, block_size);
    memcpy(buffer, node, sizeof(inode));
    write_blocks(index, 1, buffer);
    free(buffer);
}

void init_superblock(){
    byte* superblock = calloc(block_size, sizeof(byte));

    // Set magic number
    memcpy(superblock, &magic, 4);

    // Set block size
    memcpy(superblock+4, block_size, 4);

    // Set number of blocks
    memcpy(superblock+8, num_blocks, 4);

    // Set iNode table size
    memcpy(superblock+12, 1, 4);

    // Set root iNode
    memcpy(superblock+16, 0, 4);
    
    // Write superblock to disk
    write_blocks(0, 1, superblock);

    free(superblock);
}



// API functions
void mksfs(int fresh)
{
    if (fresh == 1)
    {
        if(init_fresh_disk(disk_name, block_size, num_blocks)){
            printf("Error: Could not create new disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }

        init_superblock();
        init_root_node();
        init_free_list();
    }
    else
    {
        if(init_disk(disk_name, block_size, num_blocks)){
            printf("Error: Could not open disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }
    }
}

