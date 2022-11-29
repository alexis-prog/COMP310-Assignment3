#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_api.h"
#include "disk_emu.h"

#define MAGIC_NUMBER 0xABCD0005
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 2048
#define NUM_FREE_BLOCKS (NUM_BLOCKS / 8 / BLOCK_SIZE + 1)

#define BLOCK_CACHE_SIZE 16
#define INODE_CACHE_SIZE 16

// Dynamic Constants
#define INODE_SIZE 64
#define INODES_PER_BLOCK (BLOCK_SIZE / INODE_SIZE)

#define DIR_ENTRY_SIZE 64
#define DIR_ENTRIES_PER_BLOCK (BLOCK_SIZE / DIR_ENTRY_SIZE)

#define MAXFILENAME (64 - 5)

// Never trust char size
typedef uint8_t byte_t;

char* disk_name = "disk.sfs";

// Fixed size of 1024 bytes
typedef struct _block_t{
    byte_t data[BLOCK_SIZE];
} block_t;

// INODE - 64 bytes -> 16 per block
typedef struct _inode_t {
    uint32_t mode;
    uint32_t link_count;
    uint32_t size;

    uint32_t direct[12];
    uint32_t indirect;
} inode_t;


// Fixed size of 64 bytes -> 16 per block
typedef struct _dir_entry_t {
    byte_t valid;
    uint32_t inode;
    char filename[MAXFILENAME];
} dir_entry_t;

// Superblock representation (fixed at 1024 byte for ease of use)
typedef struct _superblock_t {
    uint32_t magic;
    uint32_t block_size;
    uint32_t file_system_size;
    uint32_t inode_table_length;
    uint32_t root_dir_inode;
    byte_t padding[BLOCK_SIZE - 5*sizeof(uint32_t)];
} superblock_t;


// In-memory
superblock_t *superblock = NULL;

// Block Cache
block_t block_cache[BLOCK_CACHE_SIZE];
uint32_t block_cache_index[BLOCK_CACHE_SIZE];
uint16_t block_cache_age[BLOCK_CACHE_SIZE];
uint16_t block_rolling_counter = 0;

// Inode Cache
inode_t inode_cache[INODE_CACHE_SIZE];
uint32_t inode_cache_index[INODE_CACHE_SIZE];
uint16_t inode_cache_age[INODE_CACHE_SIZE];
uint16_t inode_rolling_counter = 0;

// Block cache management
uint32_t get_oldest_block(){
    uint32_t oldest_index = -1;
    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        if(block_cache_index[i] == -1){
            return i;
        }
        if(block_cache_age[i] < block_cache_age[oldest_index]){
            oldest_index = i;
        }
    }
    write_blocks(block_cache_index[oldest_index], 1, block_cache[oldest_index].data);
    block_rolling_counter++;
    return oldest_index;
}

void _write_block(uint32_t block_num, block_t* block){

    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        if(block_cache_index[i] == block_num){
            memcpy(block_cache[i].data, block->data, BLOCK_SIZE);
            block_cache_age[i] = block_rolling_counter;
            return;
        }
    }

    unsigned int oldest = get_oldest_block();

    if(oldest == -1){
        printf("Error: Failed to find oldest block in cache\n");
        return;
    }

    // Update cache
    memcpy(block_cache[oldest].data, block->data, BLOCK_SIZE);
    block_cache_index[oldest] = block_num;
    block_cache_age[oldest] = block_rolling_counter;
}

void _read_block(uint32_t block_num, block_t* block){

    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        if(block_cache_index[i] == block_num){
            memcpy(block->data, block_cache[i].data, BLOCK_SIZE);
            block_cache_age[i] = block_rolling_counter;
            return;
        }
    }

    unsigned int oldest = get_oldest_block();

    if(oldest == -1){
        printf("Error: Failed to find oldest block in cache");
    }

    // Update cache
    read_blocks(block_num, 1, block->data);
    memcpy(block_cache[oldest].data, block->data, BLOCK_SIZE);
    block_cache_index[oldest] = block_num;
    block_cache_age[oldest] = block_rolling_counter;
}

int is_block_free(uint32_t block_num){
    uint32_t block_index = block_num / 8 / BLOCK_SIZE;
    
    block_t block;
    _read_block(superblock->file_system_size - 1 -  block_index, &block);

   return (block.data[block_num / 8 % BLOCK_SIZE] & (1 << (block_num % 8))) == 0;
}

void set_block_status(uint32_t block_num, int status){
    uint32_t block_index = block_num / 8 / BLOCK_SIZE;
    
    block_t block;
    _read_block(superblock->file_system_size - 1 -  block_index, &block);

    if(status == 1){
        block.data[block_num / 8 % BLOCK_SIZE] |= (1 << (block_num % 8));
    } else {
        block.data[block_num / 8 % BLOCK_SIZE] &= ~(1 << (block_num % 8));
    }
}

uint32_t get_next_free_block(){
    for(int i = NUM_FREE_BLOCKS - 1; i >= 0; i--){
        block_t block;
        _read_block(superblock->file_system_size - 1 - i, &block);

        for(int j = BLOCK_SIZE-1; j >= 0; j--){
            if(block.data[j] != 0xFF){
                for(int k = 0; k < 8; k++){
                    if((block.data[j] & (1 << k)) == 0){
                        return i * BLOCK_SIZE * 8 + j * 8 + k;
                    }
                }
            }
        }
    }

    printf("Error: No free blocks\n");
    exit(1);
}

// I-Node management
void write_inode_to_disk(uint32_t inode_num, inode_t* inode){
    uint32_t block_num = inode_num / INODES_PER_BLOCK;

    if(block_num > superblock->inode_table_length){
        if(is_block_free(block_num)){
            set_block_status(block_num, 1);
            superblock->inode_table_length++;
            _write_block(0, (block_t*)superblock);
        } else {
            printf("Error: Block %d is not free, failed contiguous allocation of i-node table\n", block_num);
            return;
        }
    }

    block_t block;
    _read_block(block_num + 1, &block);

    memcpy(block.data + (inode_num % INODES_PER_BLOCK) * sizeof(inode_t), inode, sizeof(inode_t));
}

uint32_t get_oldest_inode(){
    uint32_t oldest_index = -1;
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] == -1){
            return i;
        }
        if(inode_cache_age[i] < inode_cache_age[oldest_index]){
            oldest_index = i;
        }
    }

    uint32_t inode_block_num = inode_cache_index[oldest_index] / INODES_PER_BLOCK;

    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] / INODES_PER_BLOCK == inode_block_num){
            write_inode_to_disk(inode_cache_index[i], &inode_cache[i]);
        }
    }

    inode_rolling_counter++;
    return oldest_index;
}

void get_inode(uint32_t inode_num, inode_t* inode){
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] == inode_num){
            inode_cache_age[i] = inode_rolling_counter;
            memcpy(inode, &inode_cache[i], sizeof(inode_t));
        }
    }

    unsigned int oldest = get_oldest_inode();

    if(oldest == -1){
        printf("Error: Failed to find oldest inode in cache\n");
        exit(1);
    }

    uint32_t inode_block_num = inode_num / INODES_PER_BLOCK;

    block_t block;
    _read_block(inode_block_num + 1, &block);

    memcpy(&inode_cache[oldest], block.data + (inode_num % INODES_PER_BLOCK) * sizeof(inode_t), sizeof(inode_t));
    memcpy(inode, block.data + (inode_num % INODES_PER_BLOCK) * sizeof(inode_t), sizeof(inode_t));
    inode_cache_index[oldest] = inode_num;
    inode_cache_age[oldest] = inode_rolling_counter;

    inode_rolling_counter++;
}

uint32_t get_next_free_inode(){
    for(int i = 0; i < superblock->inode_table_length; i++){
        block_t block;
        _read_block(i + 1, &block);

        for(int j = 0; j < INODES_PER_BLOCK; j++){
            inode_t* inode = (inode_t*)(block.data + j * sizeof(inode_t));
            if(inode->link_count == 0){
                return i * INODES_PER_BLOCK + j;
            }
        }
    }

    return superblock->inode_table_length * INODES_PER_BLOCK + 1;
}

void write_inode(inode_t* node, uint32_t index){
    uint32_t block_num = index / INODES_PER_BLOCK;
    if(block_num > superblock->inode_table_length){
        if(block_num + 1 != superblock->inode_table_length){
            printf("Error: Failed contiguous allocation of i-node table\n");
            return;
        }
        if(is_block_free(block_num+1)){
            set_block_status(block_num, 1);
            superblock->inode_table_length++;
        } else {
            printf("Error: Block %d is not free, failed contiguous allocation of i-node table\n", block_num);
            return;
        }
    }

    uint32_t oldest = get_oldest_inode();
    memcpy(&inode_cache[oldest], node, sizeof(inode_t));
    inode_cache_index[oldest] = index;
    inode_cache_age[oldest] = inode_rolling_counter;

    inode_rolling_counter++;
}


// Initialization helper functions
void init_superblock(){
    superblock->magic = MAGIC_NUMBER;
    superblock->block_size = BLOCK_SIZE;
    superblock->file_system_size = NUM_BLOCKS;
    superblock->inode_table_length = 1;
    superblock->root_dir_inode = 0;

    write_blocks(0, 1, (void *) superblock);
}

void init_root_node(){
    inode_t root_node;
    
    uint32_t dir_block = get_next_free_block();
    set_block_status(dir_block, 1);
    block_t *empty_block = calloc(1, sizeof(block_t));
    _write_block(dir_block, empty_block);

    root_node.mode = 0;
    root_node.link_count = 1;
    root_node.size = 0;
    root_node.direct[0] = dir_block;
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
        set_block_status(NUM_FREE_BLOCKS - i - 1, 1);
    }
}



// API functions
void mksfs(int fresh)
{
    // Invalidate cache upon initialization
    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        block_cache_index[i] = -1;
    }
    
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        inode_cache_index[i] = -1;
    }
    

    superblock = calloc(1, sizeof(superblock_t));

    if (fresh == 1)
    {
        if(init_fresh_disk(disk_name, BLOCK_SIZE, NUM_BLOCKS)){
            printf("Error: Could not create new disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }

        init_superblock();
        init_root_node();
        init_free_list();
    }
    else
    {
        if(init_disk(disk_name, BLOCK_SIZE, NUM_BLOCKS)){
            printf("Error: Could not open disk file - Aborting %s\n\n", disk_name);
            exit(1);
        }

        _read_block(0, (void *) superblock);
        // todo assert
    }
}


int sfs_getnextfilename(char* name){
    return 0;
}

int sfs_getfilesize(const char* name){
    return 0;
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
