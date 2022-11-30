#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_api.h"
#include "sfs_block.h"
#include "sfs_inode.h"
#include "sfs_dir.h"
#include "disk_emu.h"

char* disk_name = "disk.sfs";


// Initialization helper functions
void init_superblock(){
    superblock_t *superblock = get_superblock();
    superblock->magic = MAGIC_NUMBER;
    superblock->block_size = BLOCK_SIZE;
    superblock->file_system_size = NUM_BLOCKS;
    superblock->inode_table_length = 1;
    superblock->root_dir_inode = 0;

    _write_block(0, (block_t*)superblock);
    //write_blocks(0, 1, (void *) superblock);
}

void init_root_node(){
    inode_t root_node;
    
    uint32_t dir_block = get_next_free_block();
    set_block_status(dir_block, 1);
    block_t *empty_block = calloc(1, sizeof(block_t));
    _write_block(dir_block, empty_block);

    root_node.mode = 0;
    root_node.link_count = 1;
    root_node.size = BLOCK_SIZE;
    root_node.direct[0] = dir_block;
    for(int i = 1; i < INODE_DIRECT_ACCESS; i++){
        root_node.direct[i] = -1;
    }
    root_node.indirect = -1;

    write_inode(&root_node, 0);
}

void init_free_list(){
    for(int i = 0; i < NUM_BLOCKS; i++){
        set_block_status(i, 0);
    }
    set_block_status(0, 1);
    set_block_status(1, 1);
    for(int i = 0; i < NUM_FREE_BLOCKS; i++){
        set_block_status(NUM_BLOCKS - i - 1, 1);
    }
}



// API functions
void mksfs(int fresh)
{
    init_block_cache();
    init_inode_cache();

    if (fresh == 1)
    {
        if(init_fresh_disk(disk_name, BLOCK_SIZE, NUM_BLOCKS)){
            printf("Error: Could not create new disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }

        init_superblock();
        init_free_list();
        init_root_node();
        flush_inode_cache();
        flush_block_cache();
    }
    else
    {
        if(init_disk(disk_name, BLOCK_SIZE, NUM_BLOCKS)){
            printf("Error: Could not open disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }

        _read_block(0, (void *) get_superblock());
        // todo assert
    }

    read_dir_table();
}


uint32_t file_iter_id = 0;

int sfs_getnextfilename(char* name){
    dir_entry_t *dir_entry = get_dir_table_entry(file_iter_id++);

    if(dir_entry == NULL){
        return 0;
    }

    strcpy(name, dir_entry->filename);

    return strlen(name);
}

int sfs_getfilesize(const char* name){
    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode, &root_node);

    int n = -1;
    while(n){
        dir_entry_t entry;
        n = read_from_inode(&root_node, 0, sizeof(dir_entry_t), (void *)&entry);

        if(entry.valid && strcmp(entry.filename, name) == 0){
            inode_t node;
            get_inode(entry.inode, &node);
            return node.size;
        }
    }

    return -1;
}


int sfs_fopen(char* name){
    return 0;
}

int sfs_fclose(int fd){
    return 0;
}

int sfs_fwrite(int fd, const char* buf, int ln){
    return 0;
}

int sfs_fread(int fd, char* buf, int ln){
    return 0;
}

int sfs_fseek(int fd, int offset){
    return 0;
}

int sfs_remove(char* name){
    return 0;
}
