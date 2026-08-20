// 参考: 《操作系统真相还原》(于渊) 第12章 系统调用

#include "./syscall.h"
#include "./signal.h"
#include "../../include/syscall_nr.h"

static inline uint32_t syscall0(uint32_t nr) {
    uint32_t retval;
    __asm__ volatile("int $0x80"
                     : "=a"(retval)
                     : "a"(nr)
                     : "memory");
    return retval;
}

static inline uint32_t syscall1(uint32_t nr, uint32_t arg1) {
    uint32_t retval;
    __asm__ volatile("int $0x80"
                     : "=a"(retval)
                     : "a"(nr), "b"(arg1)
                     : "memory");
    return retval;
}

static inline uint32_t syscall2(uint32_t nr, uint32_t arg1, uint32_t arg2) {
    uint32_t retval;
    __asm__ volatile("int $0x80"
                     : "=a"(retval)
                     : "a"(nr), "b"(arg1), "c"(arg2)
                     : "memory");
    return retval;
}

static inline uint32_t syscall3(uint32_t nr, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t retval;
    __asm__ volatile("int $0x80"
                     : "=a"(retval)
                     : "a"(nr), "b"(arg1), "c"(arg2), "d"(arg3)
                     : "memory");
    return retval;
}

uint32_t getpid(void)                 { return syscall0(SYS_GETPID); }
int32_t  write(int32_t fd, const void* buf, uint32_t count) {
    return (int32_t)syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}
int32_t  read(int32_t fd, void* buf, uint32_t count) {
    return (int32_t)syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, count);
}
void     putchar(char c)              { write(1, &c, 1); }
void     clear(void)                  { syscall0(SYS_CLEAR); }
int32_t  fork(void)                   { return (int32_t)syscall0(SYS_FORK); }
int32_t  open(const char* pathname, uint8_t flag) {
    return (int32_t)syscall2(SYS_OPEN, (uint32_t)pathname, (uint32_t)flag);
}
int32_t  close(int32_t fd)            { return (int32_t)syscall1(SYS_CLOSE, (uint32_t)fd); }
int32_t  lseek(int32_t fd, int32_t offset, uint8_t whence) {
    return (int32_t)syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}
int32_t  unlink(const char* pathname) { return (int32_t)syscall1(SYS_UNLINK, (uint32_t)pathname); }
int32_t  mkdir(const char* pathname)  { return (int32_t)syscall1(SYS_MKDIR, (uint32_t)pathname); }
int32_t  rmdir(const char* pathname)  { return (int32_t)syscall1(SYS_RMDIR, (uint32_t)pathname); }
int32_t  chdir(const char* path)      { return (int32_t)syscall1(SYS_CHDIR, (uint32_t)path); }
char*    getcwd(char* buf, uint32_t size) { return (char*)syscall2(SYS_GETCWD, (uint32_t)buf, size); }
int32_t  stat(const char* path, struct stat* buf) {
    return (int32_t)syscall2(SYS_STAT, (uint32_t)path, (uint32_t)buf);
}
struct dir* opendir(const char* name) { return (struct dir*)syscall1(SYS_OPENDIR, (uint32_t)name); }
int32_t  closedir(struct dir* dir)    { return (int32_t)syscall1(SYS_CLOSEDIR, (uint32_t)dir); }
struct dir_entry* readdir(struct dir* dir) {
    return (struct dir_entry*)syscall1(SYS_READDIR, (uint32_t)dir);
}
void     rewinddir(struct dir* dir)   { syscall1(SYS_REWINDDIR, (uint32_t)dir); }
void     ps(void)                     { syscall0(SYS_PS); }
int32_t  execv(const char* path, const char* argv[]) {
    return (int32_t)syscall2(SYS_EXECV, (uint32_t)path, (uint32_t)argv);
}
void exit(int32_t status) {
    syscall1(SYS_EXIT, (uint32_t)status);
    for (;;) {                                                 
    }
}
int32_t wait(int32_t* status) {
    return (int32_t)syscall1(SYS_WAIT, (uint32_t)status);
}
int32_t pipe(int32_t pipefd[2]) {
    return (int32_t)syscall1(SYS_PIPE, (uint32_t)pipefd);
}
void fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd) {
    syscall2(SYS_FD_REDIRECT, (uint32_t)old_local_fd, (uint32_t)new_local_fd);
}
int32_t gui_start(void) {
    return (int32_t)syscall0(SYS_GUI);
}
void* brk(void* addr) {
    return (void*)syscall1(SYS_BRK, (uint32_t)addr);
}
void* sbrk(intptr_t inc) {
    uint32_t ob = (uint32_t)syscall1(SYS_BRK, 0);
    if (inc == 0) {
        return (void*)ob;
    }
    uint32_t nb = (uint32_t)syscall1(SYS_BRK, ob + (uint32_t)inc);
    if (nb == ob && inc > 0) {
        return (void*)-1;
    }
    return (void*)ob;
}


__attribute__((naked))
void __restore(void) {
    __asm__ volatile(
        "movl %0, %%eax\n"
        "int $0x80\n"
        : : "i"(SYS_SIGRETURN) : "memory");
}

int sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
    struct sigaction kact;
    const struct sigaction* pact = act;
    if (act) {
        kact = *act;
        kact.sa_restorer = __restore;     
        kact.sa_flags |= SA_RESTORER;
        pact = &kact;
    }
    return (int)syscall3(SYS_SIGACTION, (uint32_t)sig, (uint32_t)pact, (uint32_t)old);
}

int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
    return (int)syscall3(SYS_SIGPROCMASK, (uint32_t)how, (uint32_t)set, (uint32_t)oldset);
}

int kill(pid_t pid, int sig) {
    return (int)syscall2(SYS_KILL, (uint32_t)pid, (uint32_t)sig);
}


void (*signal(int sig, void (*handler)(int)))(int) {
    struct sigaction act, old;
    act.sa_handler = handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    act.sa_restorer = __restore;
    act.sa_flags |= SA_RESTORER;
    if (sigaction(sig, &act, &old) < 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}
