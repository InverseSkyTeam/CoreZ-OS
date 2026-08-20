// 参考: Wine 的 syscall 翻译层 (gitlab.winehq.org/wine) + Linux i386 ABI (linux/arch/x86/entry/entry_32.S)
// 本 OS 的 0x80 号使用高基数 COMPAT_SYSCALL_BASE, 兼容 Linux Syscall的功能由内核 linux_compat shim 负责
#ifndef NT_LC_H
#define NT_LC_H

#include <stdint.h>

#define LC_SYSCALL_BASE 0x50000
#define LC_PID           (LC_SYSCALL_BASE + 0)
#define LC_WRITE         (LC_SYSCALL_BASE + 1)
#define LC_READ          (LC_SYSCALL_BASE + 2)
#define LC_EXIT          (LC_SYSCALL_BASE + 3)
#define LC_BRK           (LC_SYSCALL_BASE + 4)
#define LC_OPEN          (LC_SYSCALL_BASE + 5)
#define LC_CLOSE         (LC_SYSCALL_BASE + 6)
#define LC_MMAP          (LC_SYSCALL_BASE + 7)
#define LC_SET_THREAD_AREA (LC_SYSCALL_BASE + 8)
#define LC_WRITEV        (LC_SYSCALL_BASE + 9)

#define LC_TLS_SELECTOR  0x33

#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_FLAGS   8
#define AT_ENTRY   9
#define AT_CLKTCK  17
#define AT_EXECFN  31

#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2

struct lc_iovec { uint32_t base; uint32_t len; };

extern uint32_t* __lc_auxv;

uint32_t __lc_syscall6(uint32_t nr, uint32_t a, uint32_t b, uint32_t c,
                       uint32_t d, uint32_t e, uint32_t f);

int* __lc_errno_ptr(void);
#define errno (*(__lc_errno_ptr()))

void __lc_tls_init(void);
void __lc_terminate(int status) __attribute__((noreturn));

long lc_getpid(void);
long lc_write(int fd, const void* buf, uint32_t n);
long lc_read(int fd, void* buf, uint32_t n);
long lc_brk(void* addr);
long lc_open(const char* path, int flags);
long lc_close(int fd);
long lc_set_thread_area(void* base);
long lc_writev(int fd, const struct lc_iovec* iov, int iovcnt);
long lc_mmap(void* addr, uint32_t len, int prot, int flags, int fd, uint32_t off);

uint32_t lc_strlen(const char* s);
int lc_strcmp(const char* a, const char* b);
void lc_puts(const char* s);
void lc_putc(char c);
void lc_puthex(uint32_t v);
void lc_putuint(uint32_t v);
unsigned long lc_auxv_get(int tag);

#endif