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
#define NUM_BLOCKS 32
#define NUM_FREE_BLOCKS (NUM_BLOCKS / 8 / BLOCK_SIZE + 1)
#define POINTER_SIZE 4

#define BLOCK_CACHE_SIZE 16
#define INODE_CACHE_SIZE 16

// Dynamic Constants
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

    uint32_t direct[INODE_DIRECT_ACCESS];
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
uint16_t block_rolling_counter = 1;

// Inode Cache
inode_t inode_cache[INODE_CACHE_SIZE];
uint32_t inode_cache_index[INODE_CACHE_SIZE];
uint16_t inode_cache_age[INODE_CACHE_SIZE];
uint16_t inode_rolling_counter = 1;

// Block cache management
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
            exit(1);
        }
    }

    block_t block;
    _read_block(block_num + 1, &block);

    memcpy(block.data + (inode_num % INODES_PER_BLOCK) * sizeof(inode_t), inode, sizeof(inode_t));

    _write_block(block_num + 1, &block);
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

    if(offset + size > node->size){
        printf("Error: Attempted to read past end of file\n");
        return -1;
    }

    uint32_t bytes_read = 0;
    while(bytes_read < size){
        uint32_t block_index = node->direct[block_num];

        block_t block;
        _read_block(block_index, &block);

        uint32_t bytes_to_read = BLOCK_SIZE - block_offset;
        if(bytes_to_read > size - bytes_read){
            bytes_to_read = size - bytes_read;
        }

        memcpy((byte_t *) buffer + bytes_read, block.data + block_offset, bytes_to_read);

        bytes_read += bytes_to_read;
        block_num++;
        block_offset = 0;
    }

    return bytes_read;
}

int write_to_inode(inode_t* node, uint32_t offset, byte_t* data, uint32_t length){
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
        if(current_block < new_size / BLOCK_SIZE){
            for(int i = current_block; i < new_size / BLOCK_SIZE; i++){
                uint32_t block_index = get_next_free_block();

                if(i < INODE_DIRECT_ACCESS){
                    node->direct[i] = block_index;
                }else{
                    if(node->indirect == 0){
                        node->indirect = get_next_free_block();
                    }

                    block_t block;
                    _read_block(node->indirect, &block);

                    memcpy(block.data + (i - INODE_DIRECT_ACCESS) * sizeof(uint32_t), &block_index, sizeof(uint32_t));

                    _write_block(node->indirect, &block);
                }
            }
        }

        node->size = new_size;
    }

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



// Initialization helper functions
void init_superblock(){
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
        set_block_status(NUM_BLOCKS - i - 1, 1);
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

        _read_block(0, (void *) superblock);
        // todo assert
    }
}


uint32_t file_iter_id = 0;

int sfs_getnextfilename(char* name){
    inode_t root_node;
    get_inode(superblock->root_dir_inode, &root_node);

    dir_entry_t entry;
    int n = read_from_inode(&root_node, file_iter_id * sizeof(dir_entry_t), sizeof(dir_entry_t), (void *)&entry);

    if(n == 0 || entry.valid == 0){
        return 0;
    }

    file_iter_id++;

    strcpy(name, entry.filename);
    return strlen(name);
}

int sfs_getfilesize(const char* name){
    inode_t root_node;
    get_inode(superblock->root_dir_inode, &root_node);

    int n = -1;
    while(n){
        dir_entry_t entry;
        n = read_from_inode(&root_node, 0, sizeof(dir_entry_t), (void *)&entry);

        if(entry.valid && (entry.filename, name) == 0){
            inode_t node;
            get_inode(entry.inode, &node);
            return node.size;
        }
    }

    return -1;
}

inode_t *open_inodes[MAX_OPEN_FILES];
uint32_t open_inodes_number[MAX_OPEN_FILES];

int next_free_open_file(){
    for(int i = 0; i < MAX_OPEN_FILES; i++){
        if(open_inodes_number[i] <= 0){
            return i;
        }
    }
    return -1;
}


int sfs_fopen(char* name){
    inode_t root_node;
    get_inode(superblock->root_dir_inode, &root_node);

    int n = -1;
    int empty_slot = -1;
    // open file if it exists
    while(n){
        dir_entry_t entry;
        n = read_from_inode(&root_node, 0, sizeof(dir_entry_t), (void *)&entry);

        if(entry.valid && strcmp(entry.filename, name) == 0){
            int i = next_free_open_file();
            if(i < 0){
                return -1;
            }
            
            inode_t *node = calloc(1, sizeof(inode_t));
            get_inode(entry.inode, node);
            open_inodes[i] = node;

            open_inodes_number[i] = entry.inode;

            return i;
        }else{
            if(empty_slot < 0 && entry.valid == 0){
                empty_slot = n;
            }
        }
    }

    // create file if it doesn't exist
    // todo when check n == 0, can expand?
    if(empty_slot < 0){
        printf("Error: No free slots in root directory\n");
        return -1;
    }

    inode_t *node = calloc(1, sizeof(inode_t));
    node->mode = 0;
    node->link_count = 1;
    node->size = 0;

    dir_entry_t entry;
    entry.valid = 1;
    entry.inode = get_next_free_inode();
    strcpy(entry.filename, name);

    write_to_inode(&root_node, empty_slot * sizeof(dir_entry_t), sizeof(dir_entry_t), (void *)&entry);
    write_to_inode(&root_node, entry.inode * sizeof(inode_t), sizeof(inode_t), (void *)node);
    
    int i = next_free_open_file();
    open_inodes[i] = node;
    open_inodes_number[i] = entry.inode;

    return i;
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
