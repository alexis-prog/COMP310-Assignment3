#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_block.h"
#include "sfs_inode.h"
#include "sfs_dir.h"
#include "sfs_api.h"

// Dynamic array of directory entries
dir_entry_t *dir_table;
int dir_table_size;

// Load directory entries from disk into memory
void read_dir_table(){
    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode , &root_node);
    dir_table_size = root_node.size / sizeof(dir_entry_t);

    if(dir_table != NULL){
        free(dir_table);
    }

    dir_table = calloc(dir_table_size, sizeof(dir_entry_t));
    read_from_inode(&root_node, 0, root_node.size, dir_table);
}

// Get n-th directory entry
dir_entry_t* get_dir_table_entry(int i){
    if(i < 0 || i >= dir_table_size){
        return NULL;
    }
    return dir_table + i;
}

// Get directory entry array size
int get_dir_table_size(){
    return dir_table_size;
}

// Write the directory table to disk
void write_dir_table(){
    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode, &root_node);

    write_to_inode(get_superblock()->root_dir_inode, &root_node, 0, (byte_t *) dir_table, dir_table_size * sizeof(dir_entry_t));
}

// Update the n-th entry in the directory table
void write_to_dir_table(int i, dir_entry_t *entry){
    if(i < 0){
        return;
    }

    if(i >= dir_table_size){
        dir_table_size = i + 1;
        dir_table = realloc(dir_table, dir_table_size * sizeof(dir_entry_t));
    }

    memcpy(dir_table + i, entry, sizeof(dir_entry_t));

    write_dir_table();
}

// Find a free directory entry from the table
int get_free_dir_table_entry(){
    for(int i = 0; i < dir_table_size; i++){
        if(dir_table[i].inode == 0){
            return i;
        }
    }
    return dir_table_size;
}


// Unregister a file from the directory table
int remove_from_dir_table(int i){
    if(i < 0 || i >= dir_table_size){
        return -1;
    }

    dir_table[i].valid = 0;
    int n = dir_table[i].inode;

    // Realloc failed here b/c POSIX doesn't require it to allow shrinking
    // Decided to still implement wasteful resizing to avoid management issues
    dir_entry_t* tmp = calloc(dir_table_size - 1, sizeof(dir_entry_t));
    for(int j = 0; j < i; j++){
        memcpy(tmp + j, dir_table + j, sizeof(dir_entry_t));
    }

    for(int j = i + 1; j < dir_table_size; j++){
        memcpy(tmp + j - 1, dir_table + j, sizeof(dir_entry_t));
    }

    free(dir_table);
    dir_table = tmp;

    dir_table_size--;

    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode , &root_node);

    root_node.size -= sizeof(dir_entry_t);
    write_inode(&root_node, get_superblock()->root_dir_inode);

    write_dir_table();

    return n;
}