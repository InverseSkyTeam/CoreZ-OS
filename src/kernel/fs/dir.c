#include "dir.h"
#include "inode.h"
#include "fs.h"
#include "ext2.h"
#include "../memory/pool/pool.h"
#include "../lib/str/str.h"

struct dir root_dir;

void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part, 2);
    root_dir.dir_pos = 0;
}

struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    struct dir* pdir = (struct dir*)get_kernel_pages(1);
    if (pdir == NULL) {
        return NULL;
    }
    memset(pdir, 0, PAGE_SIZE);
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

void dir_close(struct dir* dir) {
    if (dir == NULL || dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    free_kernel_page((uint32_t)dir);
}

int dir_lookup(struct dir* pdir, const char* name, struct dir_entry* dir_e) {
    uint32_t pos = 0;
    struct dir_entry de;
    while (ext2_dir_next(pdir->inode, &pos, &de) == 0) {
        if (strcmp(de.filename, name) == 0) {
            memcpy(dir_e, &de, sizeof(struct dir_entry));
            return 0;
        }
    }
    return -1;
}

struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    if (ext2_dir_next(dir->inode, &dir->dir_pos, dir_e)) {
        return NULL;
    }
    return dir_e;
}

int dir_is_empty(struct dir* dir) {
    return 0;
}

int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir) {
    return -1;
}