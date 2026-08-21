#include "file.h"
#include "inode.h"
#include "fs.h"
#include "ext2.h"
#include "../thread/thread.h"

struct file file_table[MAX_FILE_OPEN];

int fd_install(int32_t global_fd_idx) {
    uint32_t local_fd = 3;
    while (local_fd < MAX_FILES_OPEN_PER_PROC) {
        if (current_task->fd_table[local_fd] == (uint32_t)-1) {
            current_task->fd_table[local_fd] = (uint32_t)global_fd_idx;
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
    current_task->fd_table[local_fd] = (uint32_t)-1;
    return 0;
}

uint32_t fd_local2global(uint32_t local_fd) {
    return current_task->fd_table[local_fd];
}

uint32_t file_read(struct file* file, void* buf, uint32_t count) {
    int r = ext2_read_from_inode(file->fd_inode, file->fd_pos, buf, count);
    file->fd_pos += (uint32_t)r;
    return (uint32_t)r;
}

uint32_t file_write(struct file* file, const void* buf, uint32_t count) {
    int r = ext2_write_to_inode(file->fd_inode, file->fd_pos, buf, count);
    file->fd_pos += (uint32_t)r;
    return (uint32_t)r;
}