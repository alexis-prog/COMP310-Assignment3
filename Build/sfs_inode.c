#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "sfs_api.h"
#include "sfs_inode.h"
#include "sfs_block.h"
#include "disk_emu.h"

// Inode Cache
inode_t inode_cache[INODE_CACHE_SIZE];
uint32_t inode_cache_index[INODE_CACHE_SIZE];
uint16_t inode_cache_age[INODE_CACHE_SIZE];
uint16_t inode_rolling_counter = 1;

// I-Node management
void init_inode_cache(){
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        inode_cache_index[i] = -1;
    }
}

void write_inode_to_disk(uint32_t inode_num, inode_t* inode){
    uint32_t block_num = inode_num / INODES_PER_BLOCK;

    if(block_num > get_superblock()->inode_table_length){
        if(is_block_free(block_num)){
            set_block_status(block_num, 1);
            get_superblock()->inode_table_length++;
            _write_block(0, (block_t*)get_superblock());
        } else {
            printf("Error: Block %d is not free, failed contiguous allocation of i-node table\n", block_num);
            exit(1);
        }
    }

    block_t block;
    _read_block(block_num + 1, &block);

    memcpy(block.data + (inode_num % INODES_PER_BLOCK) * sizeof(inode_t), inode, sizeof(inode_t));

    _write_block(block_num + 1, &block);
}

uint32_t get_oldest_inode(){
    uint32_t oldest_index = 0;
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

void get_inode(uint32_t inode_num, inode_t *inode){
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] == inode_num){
            inode_cache_age[i] = inode_rolling_counter;
            memcpy(inode, &(inode_cache[i]), sizeof(inode_t));
            return;
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
    for(int i = 0; i < get_superblock()->inode_table_length; i++){
        block_t block;
        _read_block(i + 1, &block);

        for(int j = 0; j < INODES_PER_BLOCK; j++){
            inode_t* inode = (inode_t*)(block.data + j * sizeof(inode_t));
            if(inode->link_count == 0){
                return i * INODES_PER_BLOCK + j;
            }
        }
    }

    return get_superblock()->inode_table_length * INODES_PER_BLOCK + 1;
}

void write_inode(inode_t* node, uint32_t index){
    uint32_t block_num = index / INODES_PER_BLOCK;
    if(block_num > get_superblock()->inode_table_length){
        if(block_num + 1 != get_superblock()->inode_table_length){
            printf("Error: Failed contiguous allocation of i-node table\n");
            return;
        }
        if(is_block_free(block_num+1)){
            set_block_status(block_num, 1);
            get_superblock()->inode_table_length++;
        } else {
            printf("Error: Block %d is not free, failed contiguous allocation of i-node table\n", block_num);
            return;
        }
    }

    uint32_t cache_index = get_oldest_inode();
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] == index){
            cache_index = i;
            break;
        }
    }
    memcpy(&inode_cache[cache_index], node, sizeof(inode_t));
    inode_cache_index[cache_index] = index;
    inode_cache_age[cache_index] = inode_rolling_counter;

    inode_rolling_counter++;
}

void flush_inode_cache(){
    for(int i = 0; i < INODE_CACHE_SIZE; i++){
        if(inode_cache_index[i] != -1){
            write_inode_to_disk(inode_cache_index[i], &inode_cache[i]);
        }
    }
}

int read_from_inode(inode_t* node, uint32_t offset, uint32_t size, void* buffer){
    uint32_t block_num = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;

    uint32_t bytes_read = 0;
    uint32_t real_size = node->size - offset;
    while(bytes_read < size && real_size > 0){

        uint32_t block_index;
        if(block_num < INODE_DIRECT_ACCESS){
            block_index = node->direct[block_num];
        }else{
            block_t indirect;
            _read_block(node->indirect, &indirect);

            memcpy(&block_index, indirect.data + (block_num - INODE_DIRECT_ACCESS) * sizeof(uint32_t), sizeof(uint32_t));
        }

        block_t block;
        _read_block(block_index, &block);

        uint32_t bytes_to_read = BLOCK_SIZE - block_offset;
        if(bytes_to_read > size - bytes_read){
            bytes_to_read = size - bytes_read;
        }
        if(real_size < bytes_to_read){
            bytes_to_read = real_size;
        }

        memcpy((byte_t *) buffer + bytes_read, block.data + block_offset, bytes_to_read);
    
        bytes_read += bytes_to_read;
        real_size -= bytes_to_read;
        block_num++;
        block_offset = 0;
    }

    return bytes_read;
}

int write_to_inode(uint32_t inode_index, inode_t* node, uint32_t offset, byte_t* data, uint32_t length){
    uint32_t block_num = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;

    // expand file if necessary
    uint32_t new_size = offset + length;
    if(new_size > node->size){
        if((new_size) / BLOCK_SIZE >= INODE_MAX_BLOCKS){
            printf("Error: Attempted to write past max file size\n");
            return -1;
        }

        uint32_t current_block = node->size / BLOCK_SIZE;

        if(current_block < INODE_DIRECT_ACCESS){
            if(node->direct[current_block] != -1){
                current_block++;
            }
        }else{
            if(node->indirect != -1){
                block_t indirect;
                _read_block(node->indirect, &indirect);
                
                uint32_t indirect_block;
                memcpy(&indirect_block, indirect.data + (current_block - INODE_DIRECT_ACCESS) * sizeof(uint32_t), sizeof(uint32_t));

                if(indirect_block != -1){
                    current_block++;
                }
            }
        }

        for(int i = current_block; i < new_size / BLOCK_SIZE + 1; i++){
            uint32_t block_index = get_next_free_block();
            set_block_status(block_index, 1);

            if(i < INODE_DIRECT_ACCESS){
                node->direct[i] = block_index;
            }else{
                if(node->indirect == -1){
                    node->indirect = get_next_free_block();
                    set_block_status(node->indirect, 1);
                }

                block_t block;
                _read_block(node->indirect, &block);

                memcpy(block.data + (i - INODE_DIRECT_ACCESS) * sizeof(uint32_t), &block_index, sizeof(uint32_t));

                _write_block(node->indirect, &block);
            }

            
        }


        node->size = new_size;
    }

    write_inode(node, inode_index);

    uint32_t bytes_written = 0;
    while(bytes_written < length){
        uint32_t block_index;
        if(block_num < INODE_DIRECT_ACCESS){
            block_index = node->direct[block_num];
        }else{
            block_t block;
            _read_block(node->indirect, &block);

            memcpy(&block_index, block.data + (block_num - INODE_DIRECT_ACCESS) * sizeof(uint32_t), sizeof(uint32_t));
        }

        block_t block;
        _read_block(block_index, &block);

        uint32_t bytes_to_write = BLOCK_SIZE - block_offset;
        if(bytes_to_write > length - bytes_written){
            bytes_to_write = length - bytes_written;
        }

        memcpy(block.data + block_offset, data + bytes_written, bytes_to_write);

        _write_block(block_index, &block);

        bytes_written += bytes_to_write;
        block_num++;
        block_offset = 0;
    }

    return bytes_written;
}

void remove_inode(uint32_t index){
    inode_t node;
    get_inode(index, &node);

    node.size = 0;
    node.link_count--;

    if(node.link_count <=0){
        for(int i = 0; i < INODE_DIRECT_ACCESS; i++){
            if(node.direct[i] != -1){
                set_block_status(node.direct[i], 0);
                node.direct[i] = -1;
            }
        }

        if(node.indirect != -1){
            block_t indirect;
            _read_block(node.indirect, &indirect);

            for(int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++){
                uint32_t block_index;
                memcpy(&block_index, indirect.data + i * sizeof(uint32_t), sizeof(uint32_t));

                if(block_index != -1){
                    set_block_status(block_index, 0);
                }
            }

            set_block_status(node.indirect, 0);
            node.indirect = -1;
        }
    }

    write_inode(&node, index);
}