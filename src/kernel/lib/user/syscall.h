// 参考: 《操作系统真相还原》(于渊) 第12章 系统调用

#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>
#include "../../include/syscall_nr.h"

struct stat;
struct dir;
struct dir_entry;

#define PROT_NONE    0
#define PROT_READ    1
#define PROT_WRITE   2
#define PROT_EXEC    4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS

#define MAP_FAILED    ((void*)-1)

uint32_t getpid(void);
int32_t  write(int32_t fd, const void* buf, uint32_t count);
int32_t  read(int32_t fd, void* buf, uint32_t count);
void     putchar(char c);
void     clear(void);
int32_t  fork(void);
int32_t  open(const char* pathname, uint8_t flag);
int32_t  close(int32_t fd);
int32_t  lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t  unlink(const char* pathname);
int32_t  mkdir(const char* pathname);
int32_t  rmdir(const char* pathname);
int32_t  chdir(const char* path);
char*    getcwd(char* buf, uint32_t size);
int32_t  stat(const char* path, struct stat* buf);
struct dir* opendir(const char* name);
int32_t  closedir(struct dir* dir);
struct dir_entry* readdir(struct dir* dir);
void     rewinddir(struct dir* dir);
void     ps(void);
int32_t  execv(const char* path, const char* argv[]);
void     exit(int32_t status);
int32_t  wait(int32_t* status);
int32_t  pipe(int32_t pipefd[2]);
void     fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd);
int32_t  gui_start(void);
void*    brk(void* addr);
void*    sbrk(intptr_t inc);
void*    mmap(void* addr, uint32_t len, int prot, int flags, int fd, uint32_t offset);
int32_t  munmap(void* addr, uint32_t len);
int32_t  mprotect(void* addr, uint32_t len, int prot);
int32_t  futex(uint32_t uaddr, int op, uint32_t val, void* timeout);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

#endif
