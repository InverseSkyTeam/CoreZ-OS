#ifndef FS_EXT2_H
#define FS_EXT2_H

#include <stdint.h>
#include "inode.h"
#include "fs.h"

struct partition;

int  ext2_init(void);
struct partition* ext2_partition(void);
int  ext2_lookup(const char* path, uint32_t* ino, int* is_dir);
int  ext2_read_inode(uint32_t ino, struct inode* out);
int  ext2_read_from_inode(const struct inode* ino, uint32_t off, void* buf, uint32_t count);
int  ext2_dir_next(const struct inode* dino, uint32_t* pos, struct dir_entry* out);

#endif