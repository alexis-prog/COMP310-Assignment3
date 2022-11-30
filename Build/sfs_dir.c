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

dir_entry_t *dir_table;
int dir_table_size;

void read_dir_table(){
    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode , &root_node);
    dir_table_size = root_node.size / sizeof(dir_entry_t);

    /*if(dir_table != NULL){
        free(dir_table);
    }*/

    dir_table = calloc(dir_table_size, sizeof(dir_entry_t));
    read_from_inode(&root_node, 0, root_node.size, dir_table);
}

dir_entry_t* get_dir_table_entry(int i){
    if(i < 0 || i >= dir_table_size){
        return NULL;
    }
    return dir_table + i;
}

int get_dir_table_size(){
    return dir_table_size;
}

void write_dir_table(){
    inode_t root_node;
    get_inode(get_superblock()->root_dir_inode, &root_node);
    write_to_inode(&root_node, 0, (byte_t *) dir_table, root_node.size);
}

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

int get_free_dir_table_entry(){
    for(int i = 0; i < dir_table_size; i++){
        if(dir_table[i].inode == 0){
            return i;
        }
    }
    return dir_table_size;
}

