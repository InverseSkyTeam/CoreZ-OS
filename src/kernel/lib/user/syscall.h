#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include "../../include/nt_ping_reply.h"
#include "../../include/syscall_nr.h"
#include <stdint.h>

struct stat;
struct dir;
struct dir_entry;

struct timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};
struct timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};
struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[1];
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS

#define MAP_FAILED ((void *)-1)

uint32_t getpid(void);
int32_t write(int32_t fd, const void *buf, uint32_t count);
int32_t read(int32_t fd, void *buf, uint32_t count);
void putchar(char c);
void clear(void);
int32_t fork(void);
int32_t open(const char *pathname, uint8_t flag);
int32_t close(int32_t fd);
int32_t lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t unlink(const char *pathname);
int32_t mkdir(const char *pathname);
int32_t rmdir(const char *pathname);
int32_t chdir(const char *path);
char *getcwd(char *buf, uint32_t size);
int32_t stat(const char *path, struct stat *buf);
struct dir *opendir(const char *name);
int32_t closedir(struct dir *dir);
struct dir_entry *readdir(struct dir *dir);
void rewinddir(struct dir *dir);
void ps(void);
int32_t execv(const char *path, const char *argv[]);
void exit(int32_t status);
int32_t wait(int32_t *status);
int32_t pipe(int32_t pipefd[2]);
void fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd);
int32_t gui_start(void);
void *brk(void *addr);
void *sbrk(intptr_t inc);
void *mmap(void *addr, uint32_t len, int prot, int flags, int fd,
           uint32_t offset);
void *mmap2(void *addr, uint32_t len, int prot, int flags, int fd,
            uint32_t offset);
int32_t munmap(void *addr, uint32_t len);
int32_t mprotect(void *addr, uint32_t len, int prot);
int32_t futex(uint32_t uaddr, int op, uint32_t val, void *timeout);
int32_t clone(uint32_t flags, void *child_stack);

int32_t fstat(int32_t fd, struct stat *buf);
int32_t dup(int32_t oldfd);
int32_t dup2(int32_t oldfd, int32_t newfd);
int32_t fcntl(int32_t fd, int32_t cmd, uint32_t arg);
int32_t getdents(int32_t fd, struct linux_dirent *dirp, uint32_t count);
int32_t readlink(const char *path, char *buf, uint32_t bufsiz);
int32_t access(const char *path, int32_t mode);
int32_t rename(const char *oldpath, const char *newpath);
int32_t truncate(const char *path, int32_t length);
int32_t chmod(const char *path, uint32_t mode);
int32_t clock_gettime(int32_t clk_id, struct timespec *tp);
int32_t gettimeofday(struct timeval *tv, void *tz);
int32_t nanosleep(const struct timespec *req, struct timespec *rem);
uint32_t getuid(void);
uint32_t getgid(void);
uint32_t geteuid(void);
uint32_t getegid(void);
void exit_group(int32_t status);

int32_t icmp_send(uint32_t dst, uint16_t id, uint16_t seq);
int32_t icmp_recv(struct nt_ping_reply *buf, int32_t max);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SETTLS 0x00080000

#endif
