// 参考: 《操作系统真相还原》(于渊) 第14章 文件系统

#include "file_syscall.h"
#include "../thread/thread.h"
#include "../fs/fs.h"
#include "../fs/file.h"
#include "../fs/inode.h"
#include "../fs/dir.h"
#include "../device/ide.h"
#include "../memory/pool/pool.h"
#include "../lib/str/str.h"

struct linux_dirent {
    uint32_t d_ino;        
    uint32_t d_off;         
    uint16_t d_reclen;     
    char     d_name[1];    
};

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4

static int collect_inode_blocks(struct inode* ino, uint32_t* blocks) {
    if (ino == NULL) {
        return -1;
    }
    uint32_t idx;
    for (idx = 0; idx < 12; idx++) {
        blocks[idx] = ino->i_sectors[idx];
    }
    if (ino->i_sectors[12] != 0) {
        ide_read(cur_part->my_disk, ino->i_sectors[12], blocks + 12, 1);
    }
    return 0;
}

static int32_t path_depth(const char* path) {
    int32_t cnt = 0;
    while (*path) {
        if (*path == '/') {
            cnt++;
        }
        path++;
    }
    return cnt;
}

int32_t sys_fstat(int32_t fd, void* buf) {
    if (buf == NULL || fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current_task->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file* pf = &file_table[global_fd];
    if (pf->fd_inode == NULL) {
        return -1;
    }
    struct stat* st = (struct stat*)buf;
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
    uint32_t global_fd = current_task->fd_table[oldfd];
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
    if (oldfd < 0 || oldfd >= (int32_t)MAX_FILES_OPEN_PER_PROC ||
        newfd < 0 || newfd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    if (oldfd == newfd) {
        return newfd;
    }
    uint32_t global_fd = current_task->fd_table[oldfd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }

    if (current_task->fd_table[newfd] != (uint32_t)-1) {
        close_file(newfd);
    }
    current_task->fd_table[newfd] = global_fd;
    return newfd;
}

int32_t sys_fcntl(int32_t fd, int32_t cmd, uint32_t arg) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current_task->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file* pf = &file_table[global_fd];
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

int32_t sys_getdents(int32_t fd, void* dirp, uint32_t count) {
    if (dirp == NULL || fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd = current_task->fd_table[fd];
    if (global_fd == (uint32_t)-1 || global_fd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct file* pf = &file_table[global_fd];
    if (pf->fd_inode == NULL) {
        return -1;
    }
    uint32_t blocks[140];
    memset(blocks, 0, sizeof(blocks));
    collect_inode_blocks(pf->fd_inode, blocks);

    uint8_t* buf = (uint8_t*)get_kernel_pages(1);
    if (buf == NULL) {
        return -1;
    }
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = BLOCK_SIZE / dir_entry_size;
    uint32_t written = 0;
    uint32_t b;
    for (b = 0; b < 140 && blocks[b] != 0; b++) {
        memset(buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, blocks[b], buf, 1);
        struct dir_entry* de = (struct dir_entry*)buf;
        uint32_t i;
        for (i = 0; i < dir_entry_cnt; i++) {
            if (de[i].f_type == FT_UNKNOWN) {
                continue;
            }
            uint32_t name_len = strlen(de[i].filename);
            uint16_t reclen = (uint16_t)(10u + name_len + 1u);
            if (written + reclen > count) {
                goto out;   
            }
            struct linux_dirent* ld = (struct linux_dirent*)((uint8_t*)dirp + written);
            ld->d_ino = de[i].i_no;
            ld->d_off = written + reclen;
            ld->d_reclen = reclen;
            memcpy(ld->d_name, de[i].filename, name_len + 1);
            written += reclen;
        }
    }
out:
    free_kernel_page((uint32_t)buf);
    return (int32_t)written;
}

int32_t sys_readlink(const char* path, char* buf, uint32_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    current_task->errno = 2;
    return -1;
}

int32_t sys_access(const char* path, int32_t mode) {
    if (path == NULL) {
        return -1;
    }
    (void)mode;  
    struct path_search_record rec;
    memset(&rec, 0, sizeof(rec));
    int inode_no = search_file(path, &rec);
    dir_close(rec.parent_dir);
    if (inode_no == -1) {
        current_task->errno = 2;
        return -1;
    }
    return 0;
}

int32_t sys_rename(const char* oldpath, const char* newpath) {
    if (oldpath == NULL || newpath == NULL) {
        return -1;
    }
    if (!strcmp(oldpath, newpath)) {
        return 0;
    }
    struct path_search_record old_rec, new_rec;
    memset(&old_rec, 0, sizeof(old_rec));
    memset(&new_rec, 0, sizeof(new_rec));

    int old_inode = search_file(oldpath, &old_rec);
    if (old_inode == -1) {
        dir_close(old_rec.parent_dir);
        return -1;
    }

    if (search_file(newpath, &new_rec) != -1) {
        dir_close(old_rec.parent_dir);
        dir_close(new_rec.parent_dir);
        return -1;
    }

    if (path_depth(newpath) != path_depth(new_rec.searched_path)) {
        dir_close(old_rec.parent_dir);
        dir_close(new_rec.parent_dir);
        return -1;
    }

    char* new_name = strrchr(new_rec.searched_path, '/');
    if (new_name == NULL) {
        dir_close(old_rec.parent_dir);
        dir_close(new_rec.parent_dir);
        return -1;
    }
    new_name++;

    uint8_t* io_buf = (uint8_t*)get_kernel_pages(1);
    if (io_buf == NULL) {
        dir_close(old_rec.parent_dir);
        dir_close(new_rec.parent_dir);
        return -1;
    }
    memset(io_buf, 0, PAGE_SIZE);

    if (!create_dir_entry(cur_part, new_rec.parent_dir,
                          (uint32_t)old_inode, new_name, old_rec.file_type)) {
        free_kernel_page((uint32_t)io_buf);
        dir_close(old_rec.parent_dir);
        dir_close(new_rec.parent_dir);
        return -1;
    }
    inode_sync(cur_part, new_rec.parent_dir->inode, io_buf);

    delete_dir_entry(cur_part, old_rec.parent_dir, (uint32_t)old_inode, io_buf);
    inode_sync(cur_part, old_rec.parent_dir->inode, io_buf);

    free_kernel_page((uint32_t)io_buf);
    dir_close(new_rec.parent_dir);
    dir_close(old_rec.parent_dir);
    return 0;
}

int32_t sys_truncate(const char* path, int32_t length) {
    if (path == NULL || length < 0) {
        return -1;
    }
    struct path_search_record rec;
    memset(&rec, 0, sizeof(rec));
    int inode_no = search_file(path, &rec);
    if (inode_no == -1) {
        dir_close(rec.parent_dir);
        return -1;
    }
    if (rec.file_type != FT_REGULAR) {
        dir_close(rec.parent_dir);
        return -1;
    }
    struct inode* ino = inode_open(cur_part, (uint32_t)inode_no);
    if (ino == NULL) {
        dir_close(rec.parent_dir);
        return -1;
    }
    if ((uint32_t)length < ino->i_size) {
        ino->i_size = (uint32_t)length; 
    }
    uint8_t* io_buf = (uint8_t*)get_kernel_pages(1);
    if (io_buf != NULL) {
        inode_sync(cur_part, ino, io_buf);
        free_kernel_page((uint32_t)io_buf);
    }
    inode_close(ino);
    dir_close(rec.parent_dir);
    return 0;
}

int32_t sys_chmod(const char* path, uint32_t mode) {
    if (path == NULL) {
        return -1;
    }
    (void)mode; 
    struct path_search_record rec;
    memset(&rec, 0, sizeof(rec));
    int inode_no = search_file(path, &rec);
    dir_close(rec.parent_dir);
    if (inode_no == -1) {
        current_task->errno = 2;
        return -1;
    }
    return 0;
}