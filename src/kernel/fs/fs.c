#include "fs.h"

#include "../initer/io/io.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "../shell/pipe.h"
#include "../thread/thread.h"
#include "dir.h"
#include "ext2.h"
#include "file.h"
#include "inode.h"

struct partition *cur_part;

void filesys_init(void) {
    if (ext2_init()) {
        kprintf("filesys: ext2 init failed\n");
        return;
    }
    cur_part = ext2_partition();
    if (cur_part == NULL) {
        return;
    }
    list_init(&cur_part->open_inodes);
    open_root_dir(cur_part);
    kprintf("filesys init done, root=%s\n", cur_part->name);
}

char *path_parse(char *pathname, char *name_store) {
    uint32_t cnt = 0;
    if (pathname[0] == '/') {
        while (*(++pathname) == '/')
            ;
    }
    while (*pathname != '/' && *pathname != 0 && cnt < MAX_FILE_NAME_LEN - 1) {
        *name_store++ = *pathname++;
        cnt++;
    }
    if (pathname[0] == 0) {
        return NULL;
    }
    return pathname;
}

int search_file(const char *pathname,
                struct path_search_record *searched_record) {
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||
        !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 2;
    }
    if (pathname[0] != '/' || strlen(pathname) <= 1) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_UNKNOWN;
        searched_record->searched_path[0] = 0;
        return -1;
    }
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(pathname, &ino, &is_dir)) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_UNKNOWN;
        searched_record->searched_path[0] = 0;
        return -1;
    }
    uint32_t i = 0;
    while (pathname[i] && i < MAX_PATH_LEN - 1) {
        searched_record->searched_path[i] = pathname[i];
        i++;
    }
    searched_record->searched_path[i] = 0;
    searched_record->file_type = is_dir ? FT_DIRECTORY : FT_REGULAR;
    searched_record->parent_dir = &root_dir;
    return (int)ino;
}

static int ext2_create_common(const char *pathname, uint32_t mode, int is_dir);
int create_file(const char *pathname) {
    return ext2_create_common(pathname, 0x8000u /*EXT2_S_IFREG*/, 0);
}

static int split_parent_path(const char *pathname, char *parent,
                             char **base_out, uint32_t buf_len) {
    uint32_t plen = (uint32_t)strlen(pathname);
    if (plen >= buf_len) {
        return -1;
    }
    memcpy(parent, pathname, plen + 1);
    uint32_t i = plen;
    while (i > 1 && parent[i - 1] == '/') {
        parent[--i] = 0;
    }
    if (i == 0) {
        return -1;
    }
    char *slash = strrchr(parent, '/');
    if (slash == NULL) {
        return -1;
    }
    *slash = 0;
    *base_out = slash + 1;
    if (slash == parent) {
        parent[0] = '/';
        parent[1] = 0;
        *base_out = parent + 1;
    }
    if ((*base_out)[0] == 0) {
        return -1;
    }
    return 0;
}

static uint32_t get_parent_inode(const char *parent) {
    uint32_t pino = 0;
    int is_dir = 0;
    if (ext2_lookup(parent, &pino, &is_dir) || !is_dir) {
        return 0;
    }
    return pino;
}

static void ext2_free_best_effort(struct inode *ino);

static int ext2_create_common(const char *pathname, uint32_t mode, int is_dir) {
    char parent[MAX_PATH_LEN];
    char *base;
    if (split_parent_path(pathname, parent, &base, MAX_PATH_LEN) ||
        base == NULL) {
        return -1;
    }
    uint32_t pino = get_parent_inode(parent);
    if (pino == 0) {
        return -1;
    }
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return -1;
    }
    uint32_t tino = 0;
    int tdir = 0;
    if (ext2_lookup(pathname, &tino, &tdir) == 0) {
        return -1;
    }
    struct inode par;
    if (ext2_read_inode(pino, &par)) {
        return -1;
    }
    struct inode newi;
    uint32_t ino = ext2_new_inode(mode, &newi);
    if (ino == 0) {
        return -1;
    }
    if (is_dir) {
        if (ext2_add_entry(&newi, ino, ".", 1) ||
            ext2_add_entry(&newi, pino, "..", 1)) {
            ext2_free_best_effort(&newi);
            return -1;
        }
    }
    if (ext2_add_entry(&par, ino, base, is_dir)) {
        ext2_free_best_effort(&newi);
        return -1;
    }
    return (int)ino;
}

static void ext2_free_best_effort(struct inode *ino) {
    ext2_truncate_inode(ino);
    ext2_write_inode(ino->i_no, ino);
    ext2_free_inode(ino->i_no);
}

int open_file(const char *pathname, uint8_t flags) {
    if (pathname[strlen(pathname) - 1] == '/') {
        return -1;
    }
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(pathname, &ino, &is_dir)) {
        if ((flags & O_CREAT) != 0) {
            if (create_file(pathname) != 0) {
                return -1;
            }
            if (ext2_lookup(pathname, &ino, &is_dir) || is_dir) {
                return -1;
            }
        } else {
            return -1;
        }
    } else if (is_dir) {
        return -1;
    }
    uint32_t global_fd_idx = 0;
    while (global_fd_idx < MAX_FILE_OPEN) {
        if (file_table[global_fd_idx].fd_inode == NULL) {
            break;
        }
        global_fd_idx++;
    }
    if (global_fd_idx >= MAX_FILE_OPEN) {
        return -1;
    }
    file_table[global_fd_idx].fd_pos = 0;
    file_table[global_fd_idx].fd_flag = flags;
    file_table[global_fd_idx].fd_inode = inode_open(cur_part, ino);
    if (file_table[global_fd_idx].fd_inode == NULL) {
        return -1;
    }
    int fd = fd_install((int32_t)global_fd_idx);
    if (fd == -1) {
        inode_close(file_table[global_fd_idx].fd_inode);
        file_table[global_fd_idx].fd_inode = NULL;
        return -1;
    }
    return fd;
}

int close_file(int fd) {
    if (fd < 3 || fd >= MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd_idx = current_task->fd_table[fd];
    if (global_fd_idx == (uint32_t)-1) {
        return -1;
    }
    struct file *file = &file_table[global_fd_idx];
    if (file->fd_flag == PIPE_FLAG) {
        if (--file->fd_pos == 0) {
            free_kernel_page((uint32_t)file->fd_inode);
            file->fd_inode = NULL;
        }
        fd_release((uint32_t)fd);
        return 0;
    }
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    file->fd_pos = 0;
    file->fd_flag = 0;
    fd_release((uint32_t)fd);
    return 0;
}

uint32_t read_file(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FILES_OPEN_PER_PROC) {
        return (uint32_t)-1;
    }
    uint32_t global_fd_idx = current_task->fd_table[fd];
    if (global_fd_idx == (uint32_t)-1) {
        return (uint32_t)-1;
    }
    return file_read(&file_table[global_fd_idx], buf, count);
}

uint32_t write_file(int fd, const void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FILES_OPEN_PER_PROC) {
        return (uint32_t)-1;
    }
    uint32_t global_fd_idx = current_task->fd_table[fd];
    if (global_fd_idx == (uint32_t)-1) {
        return (uint32_t)-1;
    }
    return file_write(&file_table[global_fd_idx], buf, count);
}

int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 3 || fd >= MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t global_fd_idx = current_task->fd_table[fd];
    if (global_fd_idx == (uint32_t)-1) {
        return -1;
    }
    struct file *pf = &file_table[global_fd_idx];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = (int32_t)pf->fd_pos + offset;
        break;
    case SEEK_END:
        new_pos = file_size + offset;
        break;
    default:
        return -1;
    }
    if (new_pos < 0 || new_pos > file_size) {
        return -1;
    }
    pf->fd_pos = (uint32_t)new_pos;
    return (int32_t)pf->fd_pos;
}

int32_t block_bitmap_alloc(struct partition *part) { return -1; }

int32_t inode_bitmap_alloc(struct partition *part) { return -1; }

void block_bitmap_free(struct partition *part, uint32_t lba) {}

void inode_bitmap_free(struct partition *part, uint32_t inode_no) {}

int sys_unlink(const char *pathname) {
    char parent[MAX_PATH_LEN];
    char *base;
    if (split_parent_path(pathname, parent, &base, MAX_PATH_LEN) ||
        base == NULL) {
        return -1;
    }
    uint32_t pino = get_parent_inode(parent);
    if (pino == 0) {
        return -1;
    }
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(pathname, &ino, &is_dir) || is_dir) {
        return -1;
    }
    struct inode par;
    struct inode obj;
    if (ext2_read_inode(pino, &par) || ext2_read_inode(ino, &obj)) {
        return -1;
    }
    if (ext2_remove_entry(&par, base)) {
        return -1;
    }
    ext2_truncate_inode(&obj);
    ext2_write_inode(obj.i_no, &obj);
    ext2_free_inode(obj.i_no);
    return 0;
}

int32_t sys_mkdir(const char *pathname) {
    if (pathname == NULL) {
        return -1;
    }
    return ext2_create_common(pathname, 0x4000u /*EXT2_S_IFDIR*/, 1) ? 0 : -1;
}

static int ext2_dir_is_empty(struct inode *dino) {
    uint32_t pos = 0;
    struct dir_entry de;
    while (ext2_dir_next(dino, &pos, &de) == 0) {
        if (strcmp(de.filename, ".") != 0 && strcmp(de.filename, "..") != 0) {
            return 0;
        }
    }
    return 1;
}

struct dir *sys_opendir(const char *name) {
    uint32_t ino = 0;
    int is_dir = 0;
    if (!strcmp(name, "/") || !strcmp(name, "/.") || !strcmp(name, "/..")) {
        ino = 2;
        is_dir = 1;
    } else if (ext2_lookup(name, &ino, &is_dir)) {
        return NULL;
    }
    if (!is_dir) {
        return NULL;
    }
    return dir_open(cur_part, ino);
}

int32_t sys_closedir(struct dir *dir) {
    int32_t ret = -1;
    if (dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

struct dir_entry *sys_readdir(struct dir *dir) { return dir_read(dir); }

void sys_rewinddir(struct dir *dir) { dir->dir_pos = 0; }

int32_t sys_rmdir(const char *pathname) {
    if (pathname == NULL) {
        return -1;
    }
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||
        !strcmp(pathname, "/..") || !strcmp(pathname, ".") ||
        !strcmp(pathname, "..")) {
        return -1;
    }
    char parent[MAX_PATH_LEN];
    char *base;
    if (split_parent_path(pathname, parent, &base, MAX_PATH_LEN) ||
        base == NULL) {
        return -1;
    }
    uint32_t pino = get_parent_inode(parent);
    if (pino == 0) {
        return -1;
    }
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(pathname, &ino, &is_dir) || !is_dir) {
        return -1;
    }
    struct inode par;
    struct inode obj;
    if (ext2_read_inode(pino, &par) || ext2_read_inode(ino, &obj)) {
        return -1;
    }
    if (!ext2_dir_is_empty(&obj)) {
        return -1;
    }
    if (ext2_remove_entry(&par, base)) {
        return -1;
    }
    ext2_truncate_inode(&obj);
    ext2_write_inode(obj.i_no, &obj);
    ext2_free_inode(obj.i_no);
    return 0;
}

static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr) {
    struct inode *ino = inode_open(cur_part, child_inode_nr);
    if (ino == NULL) {
        return (uint32_t)-1;
    }
    uint32_t pos = 0;
    struct dir_entry de;
    uint32_t parent = 0;
    while (ext2_dir_next(ino, &pos, &de) == 0) {
        if (strcmp(de.filename, "..") == 0) {
            parent = de.i_no;
            break;
        }
    }
    inode_close(ino);
    return parent;
}

static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr,
                              char *path) {
    struct inode *p = inode_open(cur_part, p_inode_nr);
    if (p == NULL) {
        return -1;
    }
    uint32_t pos = 0;
    struct dir_entry de;
    int ret = -1;
    while (ext2_dir_next(p, &pos, &de) == 0) {
        if (de.i_no == c_inode_nr && strcmp(de.filename, ".") != 0 &&
            strcmp(de.filename, "..") != 0) {
            strcat(path, "/");
            strcat(path, de.filename);
            ret = 0;
            break;
        }
    }
    inode_close(p);
    return ret;
}

char *sys_getcwd(char *buf, uint32_t size) {
    uint32_t child = current_task->cwd_inode_nr;
    if (child == 0 || child == 2) {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }
    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};
    while (child) {
        uint32_t parent = get_parent_dir_inode_nr(child);
        if (get_child_dir_name(parent, child, full_path_reverse) == -1) {
            return NULL;
        }
        child = parent;
    }
    char *last_slash;
    while ((last_slash = strrchr(full_path_reverse, '/'))) {
        uint32_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        *last_slash = 0;
    }
    return buf;
}

int32_t sys_chdir(const char *path) {
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(path, &ino, &is_dir) || !is_dir) {
        return -1;
    }
    current_task->cwd_inode_nr = ino;
    return 0;
}

int32_t sys_stat(const char *path, struct stat *buf) {
    if (!strcmp(path, ".") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 2;
        buf->st_size = 0;
        return 0;
    }
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(path, &ino, &is_dir)) {
        return -1;
    }
    struct inode *obj = inode_open(cur_part, ino);
    if (obj == NULL) {
        return -1;
    }
    buf->st_size = obj->i_size;
    inode_close(obj);
    buf->st_filetype = is_dir ? FT_DIRECTORY : FT_REGULAR;
    buf->st_ino = ino;
    return 0;
}