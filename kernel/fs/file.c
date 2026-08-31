#include "kernel/fs/file.h"
#include "kernel/sched/sync.h"
#include "kernel/sched/thread.h"
#include "kernel/fs/ext2.h"
#include "kernel/fs/fs.h"
#include "kernel/fs/inode.h"
struct file file_table[MAX_FILE_OPEN];

struct lock file_table_lock;

void file_table_init(void) {
    lock_init(&file_table_lock);
}

int file_table_alloc_slot(void) {
    lock_acquire(&file_table_lock);
    for (uint32_t i = 0; i < MAX_FILE_OPEN; i++) {
        if (file_table[i].fd_inode == NULL) {
            file_table[i].fd_inode = FILE_SLOT_RESERVED;
            lock_release(&file_table_lock);
            return (int)i;
        }
    }
    lock_release(&file_table_lock);
    return -1;
}

void file_table_free_slot(int idx) {
    if (idx < 0 || idx >= (int)MAX_FILE_OPEN) {
        return;
    }
    lock_acquire(&file_table_lock);
    file_table[idx].fd_inode = NULL;
    file_table[idx].fd_pos = 0;
    file_table[idx].fd_flag = 0;
    file_table[idx].proc_id = 0;
    file_table[idx].ref_cnt = 0;
    lock_release(&file_table_lock);
}

struct file *file_get(uint32_t gfd) {
    if (gfd >= MAX_FILE_OPEN) {
        return NULL;
    }
    return &file_table[gfd];
}

void file_table_ref(uint32_t gfd) {
    if (gfd >= MAX_FILE_OPEN) {
        return;
    }
    lock_acquire(&file_table_lock);
    file_table[gfd].ref_cnt++;
    lock_release(&file_table_lock);
}
int fd_install(int32_t global_fd_idx) {
    uint32_t local_fd = 3;
    while (local_fd < MAX_FILES_OPEN_PER_PROC) {
        if (current->fd_table[local_fd] == (uint32_t)-1) {
            current->fd_table[local_fd] = (uint32_t)global_fd_idx;
            return local_fd;
        }
        local_fd++;
    }
    return -1;
}
int fd_release(uint32_t local_fd) {
    if (local_fd >= MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    current->fd_table[local_fd] = (uint32_t)-1;
    return 0;
}
uint32_t fd_local2global(uint32_t local_fd) {
    if (local_fd >= MAX_FILES_OPEN_PER_PROC) {
        return (uint32_t)-1;
    }
    return current->fd_table[local_fd];
}
uint32_t file_read(struct file *file, void *buf, uint32_t count) {
    int r = ext2_read_from_inode(file->fd_inode, file->fd_pos, buf, count);
    file->fd_pos += (uint32_t)r;
    return (uint32_t)r;
}
uint32_t file_write(struct file *file, const void *buf, uint32_t count) {
    int r = ext2_write_to_inode(file->fd_inode, file->fd_pos, buf, count);
    file->fd_pos += (uint32_t)r;
    return (uint32_t)r;
}
