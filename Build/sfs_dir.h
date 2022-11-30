#ifndef SFS_DIR_H
#define SFS_DIR_H

// Fixed size of 64 bytes -> 16 per block
typedef struct _dir_entry_t {
    byte_t valid;
    uint32_t inode;
    char filename[MAXFILENAME];
} dir_entry_t;

#endif