
#include "file_syscall.h"
#include "../device/ide.h"
#include "../fs/dir.h"
#include "../fs/ext2.h"
#include "../fs/file.h"
#include "../fs/fs.h"
#include "../fs/inode.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "../thread/thread.h"
struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[1];
};
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
int32_t sys_fstat(int32_t fd, void *buf) {
    if (buf == NULL || fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file *pf = &file_table[global_fd];
    if (pf->fd_inode == NULL) {
        return -1;
    }
    struct stat *st = (struct stat *)buf;
    memset(st, 0, sizeof(*st));
    st->st_ino = pf->fd_inode->i_no;
    st->st_size = pf->fd_inode->i_size;
    st->st_filetype = FT_REGULAR;
    return 0;
}
int32_t sys_dup(int32_t oldfd) {
    if (oldfd < 0 || oldfd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current->fd_table[oldfd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    int newfd = fd_install((int32_t)global_fd);
    if (newfd == -1) {
        return -1;
    }
    return newfd;
}
int32_t sys_dup2(int32_t oldfd, int32_t newfd) {
    if (oldfd < 0 || oldfd >= (int32_t)MAX_FILES_OPEN_PER_PROC || newfd < 0 ||
        newfd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    if (oldfd == newfd) {
        return newfd;
    }
    uint32_t global_fd = current->fd_table[oldfd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    if (current->fd_table[newfd] != (uint32_t)-1) {
        close_file(newfd);
    }
    current->fd_table[newfd] = global_fd;
    return newfd;
}
int32_t sys_fcntl(int32_t fd, int32_t cmd, uint32_t arg) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file *pf = &file_table[global_fd];
    switch (cmd) {
    case F_DUPFD:
        (void)arg;
        return sys_dup(fd);
    case F_GETFD:
        return 0;
    case F_SETFD:
        (void)arg;
        return 0;
    case F_GETFL:
        return (int32_t)pf->fd_flag;
    case F_SETFL:
        pf->fd_flag = arg;
        return 0;
    default:
        return -1;
    }
}
int32_t sys_getdents(int32_t fd, void *dirp, uint32_t count) {
    if (dirp == NULL || fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file *pf = &file_table[global_fd];
    if (pf->fd_inode == NULL) {
        return -1;
    }
    uint32_t pos = 0;
    struct dir_entry de;
    uint32_t written = 0;
    while (ext2_dir_next(pf->fd_inode, &pos, &de) == 0) {
        uint32_t name_len = strlen(de.filename);
        uint16_t reclen = (uint16_t)(10u + name_len + 1u);
        if (written + reclen > count) {
            break;
        }
        struct linux_dirent *ld =
            (struct linux_dirent *)((uint8_t *)dirp + written);
        ld->d_ino = de.i_no;
        ld->d_off = written + reclen;
        ld->d_reclen = reclen;
        memcpy(ld->d_name, de.filename, name_len + 1);
        written += reclen;
    }
    return (int32_t)written;
}
int32_t sys_readlink(const char *path, char *buf, uint32_t bufsiz) {
    (void)path;
    (void)buf;
    (void)bufsiz;
    current->errno = 2;
    return -1;
}
int32_t sys_access(const char *path, int32_t mode) {
    if (path == NULL) {
        return -1;
    }
    (void)mode;
    struct path_search_record rec;
    memset(&rec, 0, sizeof(rec));
    int inode_no = search_file(path, &rec);
    dir_close(rec.parent_dir);
    if (inode_no == -1) {
        current->errno = 2;
        return -1;
    }
    return 0;
}
int32_t sys_rename(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    current->errno = 30;
    return -1;
}
int32_t sys_truncate(const char *path, int32_t length) {
    (void)path;
    (void)length;
    current->errno = 30;
    return -1;
}
int32_t sys_chmod(const char *path, uint32_t mode) {
    if (path == NULL) {
        return -1;
    }
    (void)mode;
    struct path_search_record rec;
    memset(&rec, 0, sizeof(rec));
    int inode_no = search_file(path, &rec);
    dir_close(rec.parent_dir);
    if (inode_no == -1) {
        current->errno = 2;
        return -1;
    }
    return 0;
}
