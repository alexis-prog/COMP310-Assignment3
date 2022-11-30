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

uint32_t opened_files[MAX_OPEN_FILES];
char *opened_files_names[MAX_OPEN_FILES];
int file_offset[MAX_OPEN_FILES];


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
    }

    read_dir_table();
    
    // opened files init
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        opened_files[i] = -1;
        file_offset[i] = -1;
    }
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
    for(int i = 0; i < get_dir_table_size(); i++){
        if(get_dir_table_entry(i) == NULL){
            continue;
        }

        if(strcmp(name, get_dir_table_entry(i)->filename) == 0){
            inode_t inode;
            get_inode(get_dir_table_entry(i)->inode, &inode);

            return inode.size;
        }
    }

    return -1;
}





int sfs_fopen(char* name){
    int free = -1;

    // check if file is already opened
    /*for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(opened_files_names[i] == NULL){
            free = i;
        }else if(strcmp(opened_files_names[i], name) == 0){
            return i;
        }
    }*/
    
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(opened_files[i] == -1){
            free = i;
        }else if(strcmp(opened_files_names[i], name) == 0){
                return i;
        }
    }

    if(free == -1){
        printf("Error: Could not open file - No free file descriptors\n\n");
        exit(1);
    }

    // check if file exists
    for(int i = 0; i < get_dir_table_size(); i++){
        if(strcmp(name, get_dir_table_entry(i)->filename) == 0){
            //opened_files_names[free] = name;
            opened_files[free] = get_dir_table_entry(i)->inode;
            opened_files_names[free] = name;

            inode_t inode;
            get_inode(opened_files[free], &inode);
            file_offset[free] = inode.size;

            return free;
        }
    }

    // create new file
    uint32_t inode_id = get_next_free_inode();
    inode_t inode;
    inode.mode = 0;
    inode.link_count = 1;
    inode.size = 0;
    for(int i = 0; i < INODE_DIRECT_ACCESS; i++){
        inode.direct[i] = -1;
    }
    inode.indirect = -1;

    write_inode(&inode, inode_id);

    dir_entry_t entry;
    entry.valid = 1;
    entry.inode = inode_id;
    strcpy(entry.filename, name);

    write_to_dir_table(get_free_dir_table_entry(), &entry);

    opened_files_names[free] = name;
    opened_files[free] = inode_id;
    file_offset[free] = 0;

    return free;
}

int sfs_fclose(int fd){
    if(fd < 0 || fd >= MAX_OPEN_FILES){
        printf("Error: Could not close file - Invalid file descriptor\n\n");
        exit(1);
    }

    /*if(opened_files_names[fd] == NULL){
        return -1;
    }*/
    if(opened_files[fd] == -1){
        return -1;
    }

    //opened_files_names[fd] = NULL;
    opened_files[fd] = -1;
    file_offset[fd] = -1;

    flush_inode_cache();
    flush_block_cache();
    return 0;
}

int sfs_fwrite(int fd, const char* buf, int ln){
    if(fd < 0 || fd >= MAX_OPEN_FILES){
        printf("Error: Could not read file - Invalid file descriptor\n\n");
        exit(1);
    }

    if(opened_files[fd] == -1){
        return -1;
    }

    inode_t inode;
    get_inode(opened_files[fd], &inode);

    int i = write_to_inode(&inode, file_offset[fd], buf, ln);
    write_inode(&inode, opened_files[fd]);

    file_offset[fd] += i;

    return i;
}

int sfs_fread(int fd, char* buf, int ln){
    if(fd < 0 || fd >= MAX_OPEN_FILES){
        printf("Error: Could not read file - Invalid file descriptor\n\n");
        exit(1);
    }

    if(opened_files[fd] == -1){
        return -1;
    }

    inode_t inode;
    get_inode(opened_files[fd], &inode);

    int i = read_from_inode(&inode, file_offset[fd], ln, buf);

    file_offset[fd] += ln;

    return i;
}

int sfs_fseek(int fd, int offset){
    if(offset < 0){
        return -1;
    }
    file_offset[fd] = offset;
    return 0;
}

int sfs_remove(char* name){

    return 0;
}
