#ifndef SFS_DIR_H
#define SFS_DIR_H

// Fixed size of 64 bytes -> 16 per block
typedef struct _dir_entry_t {
    uint32_t valid;
    uint32_t inode;
    char filename[MAXFILENAME];
} dir_entry_t;

void read_dir_table();

dir_entry_t* get_dir_table_entry(int);

int get_dir_table_size();

void write_to_dir_table(int, dir_entry_t*);

int get_free_dir_table_entry();

int remove_from_dir_table(int);

#endif