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

/* ext2 写路径 */
int  ext2_new_inode(uint32_t mode, struct inode* out);
void ext2_free_inode(uint32_t ino);
int  ext2_write_inode(uint32_t ino, const struct inode* in);
int  ext2_write_to_inode(struct inode* ino, uint32_t off, const void* buf, uint32_t count);
void ext2_truncate_inode(struct inode* ino);
int  ext2_add_entry(struct inode* dino, uint32_t ino, const char* name, int is_dir);
int  ext2_remove_entry(struct inode* dino, const char* name);

#endif