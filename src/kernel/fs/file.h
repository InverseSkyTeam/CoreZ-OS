#ifndef FS_FILE_H
#define FS_FILE_H

#include "fs.h"
#include "inode.h"
#include "../thread/sync.h"
#include <stdint.h>

#define MAX_FILE_OPEN 32

struct file {
    uint32_t fd_pos;
    uint32_t fd_flag;
    struct inode *fd_inode;
    uint32_t proc_id;
    uint32_t ref_cnt;
};

extern struct file file_table[MAX_FILE_OPEN];
extern struct lock file_table_lock;
void file_table_init(void);

int fd_install(int32_t global_fd_idx);
int fd_release(uint32_t local_fd);
uint32_t fd_local2global(uint32_t local_fd);
uint32_t file_read(struct file *file, void *buf, uint32_t count);
uint32_t file_write(struct file *file, const void *buf, uint32_t count);

#endif
