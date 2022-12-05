#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "sfs_block.h"
#include "sfs_api.h"

// Block Cache
block_t block_cache[BLOCK_CACHE_SIZE];
uint32_t block_cache_index[BLOCK_CACHE_SIZE];
uint16_t block_cache_age[BLOCK_CACHE_SIZE];
uint16_t block_rolling_counter = 1;

// In-memory
superblock_t *superblock = NULL;

superblock_t* get_superblock(){
    return superblock;
}

// Block cache management
void init_block_cache(){
    // Invalidate cache upon initialization
    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        block_cache_index[i] = -1;
    }

    superblock = calloc(1, sizeof(superblock_t));
}

uint32_t get_oldest_block(){
    uint32_t oldest_index = 0;
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

    _write_block(superblock->file_system_size - 1 -  block_index, &block);
}

uint32_t get_next_free_block(){
    for(int i = NUM_BLOCKS - 1; i >= 0; i--){
        if(is_block_free(i)){
            return i;
        }
    }

    printf("Error: No free blocks\n");
    exit(1);
}

void flush_block_cache(){
    for(int i = 0; i < BLOCK_CACHE_SIZE; i++){
        if(block_cache_index[i] != -1){
            write_blocks(block_cache_index[i], 1, block_cache[i].data);
        }
    }
}