#include "proc.h"
#include "../lib/str/str.h"
#include "../lib/user/stdio.h"
#include "../memory/pool/pool.h"
#include "../thread/thread.h"
#include "file.h"
#include "fs.h"

enum { PROC_NONE, PROC_DIR, PROC_MEMINFO };

int proc_match(const char *path) {
    if (path == NULL) {
        return 0;
    }
    if (strcmp(path, "/proc") == 0) {
        return 1;
    }
    return strncmp(path, "/proc/", 6) == 0;
}

static int proc_node_of(const char *path) {
    if (strcmp(path, "/proc") == 0) {
        return PROC_DIR;
    }
    if (strcmp(path, "/proc/meminfo") == 0) {
        return PROC_MEMINFO;
    }
    return PROC_NONE;
}

static uint32_t meminfo_build(char *dst, uint32_t cap) {
    uint32_t bytes = kernel_pool.pool_bitmap.btmp_bytes_len;
    uint32_t used = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        used += __builtin_popcount(kernel_pool.pool_bitmap.bits[i]);
    }
    uint32_t nframes = bytes * 8;
    uint32_t total_kb = nframes * (PAGE_SIZE / 1024);
    uint32_t free_kb = (nframes - used) * (PAGE_SIZE / 1024);
    uint32_t used_kb = used * (PAGE_SIZE / 1024);
    (void)cap;
    return sprintf(dst,
                   "MemTotal:     %d kB\n"
                   "MemFree:      %d kB\n"
                   "MemUsed:      %d kB\n",
                   total_kb, free_kb, used_kb);
}

static uint32_t proc_size(int node) {
    if (node != PROC_MEMINFO) {
        return 0;
    }
    char buf[128];
    return meminfo_build(buf, sizeof(buf));
}

int proc_open(const char *path, uint8_t flags) {
    int node = proc_node_of(path);
    if (node == PROC_NONE) {
        return -1;
    }
    int gfd = file_table_alloc_slot();
    if (gfd == -1) {
        return -1;
    }
    struct file *file = file_get((uint32_t)gfd);
    file->fd_pos = 0;
    file->fd_flag = flags;
    file->fd_inode = NULL;
    file->proc_id = (uint32_t)node;
    file->ref_cnt = 1;
    int fd = fd_install(gfd);
    if (fd == -1) {
        file_table_free_slot(gfd);
        return -1;
    }
    return fd;
}

uint32_t proc_read(struct file *file, void *buf, uint32_t count) {
    if (file->proc_id != PROC_MEMINFO) {
        return 0;
    }
    char tmp[128];
    uint32_t len = meminfo_build(tmp, sizeof(tmp));
    if (file->fd_pos >= len) {
        return 0;
    }
    uint32_t remain = len - file->fd_pos;
    uint32_t n = (count < remain) ? count : remain;
    memcpy(buf, tmp + file->fd_pos, n);
    file->fd_pos += n;
    return n;
}

int proc_stat(const char *path, struct stat *buf) {
    int node = proc_node_of(path);
    if (node == PROC_NONE) {
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    buf->st_ino = 1;
    if (node == PROC_DIR) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_size = 0;
    } else {
        buf->st_filetype = FT_REGULAR;
        buf->st_size = proc_size(node);
    }
    return 0;
}

int proc_fstat(struct file *file, struct stat *buf) {
    if (file->proc_id == PROC_NONE) {
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    buf->st_ino = 1;
    if (file->proc_id == PROC_DIR) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_size = 0;
    } else {
        buf->st_filetype = FT_REGULAR;
        buf->st_size = proc_size(file->proc_id);
    }
    return 0;
}

int proc_access(const char *path) {
    return proc_match(path) ? 0 : -1;
}

int proc_lseek(struct file *file, int32_t offset, uint8_t whence) {
    int32_t size = (int32_t)proc_size(file->proc_id);
    int32_t new_pos = 0;
    if (whence == SEEK_SET) {
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = (int32_t)file->fd_pos + offset;
    } else if (whence == SEEK_END) {
        new_pos = size + offset;
    } else {
        return -1;
    }
    if (new_pos < 0) {
        return -1;
    }
    file->fd_pos = (uint32_t)new_pos;
    return (int32_t)file->fd_pos;
}
