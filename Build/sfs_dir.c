#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_block.c"
#include "sfs_inode.c"
#include "sfs_api.h"

// Fixed size of 64 bytes -> 16 per block
typedef struct _dir_entry_t {
    byte_t valid;
    uint32_t inode;
    char filename[MAXFILENAME];
} dir_entry_t;

void load_dir_entries(){
    inode_t root_node;
    read_inode(&root_node, superblock->root_dir_inode);

    int num_entries = root_node.size / sizeof(dir_entry_t);
    
}