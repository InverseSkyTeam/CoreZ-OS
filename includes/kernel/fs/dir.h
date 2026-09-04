#ifndef FS_DIR_H
#define FS_DIR_H

#include "kernel/fs/fs.h"
#include "kernel/fs/inode.h"
#include <stdint.h>

#define MAX_FILE_NAME_LEN 256

struct dir {
    struct inode *inode;
    uint32_t dir_pos;
    uint8_t dir_buf[512];
};

struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];
    uint32_t i_no;
    enum file_types f_type;
};

extern struct dir root_dir;

void open_root_dir(struct partition *part);
struct dir *dir_open(struct partition *part, uint32_t inode_no);
void dir_rewind(struct dir *dir);
void dir_close(struct dir *dir);
struct dir_entry *dir_read(struct dir *dir);

#endif
