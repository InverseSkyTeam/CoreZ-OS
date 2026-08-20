// 参考: Wine ntdll/unix/syscall.c 的式 trampoline + glibc set_thread_area/errno 用法
#include "./lc.h"

static int __lc_ever_set = 0;
static uint8_t __lc_tls_block[16] __attribute__((aligned(16)));

void __lc_tls_init(void) {
    if (__lc_ever_set) return;
    if (lc_set_thread_area((void*)(uintptr_t)__lc_tls_block) != 0) return;
    __lc_ever_set = 1;
    __asm__ volatile("mov %w0, %%gs" : : "r"((uint16_t)LC_TLS_SELECTOR));
}

int* __lc_errno_ptr(void) {
    int* p;
    __asm__("movl %%gs:0, %0" : "=r"(p));
    return p;
}

long lc_getpid(void) {
    return (long)__lc_syscall6(LC_PID, 0, 0, 0, 0, 0, 0);
}

long lc_write(int fd, const void* buf, uint32_t n) {
    return (long)__lc_syscall6(LC_WRITE, (uint32_t)fd, (uint32_t)buf, n, 0, 0, 0);
}

long lc_read(int fd, void* buf, uint32_t n) {
    return (long)__lc_syscall6(LC_READ, (uint32_t)fd, (uint32_t)buf, n, 0, 0, 0);
}

long lc_brk(void* addr) {
    return (long)__lc_syscall6(LC_BRK, (uint32_t)addr, 0, 0, 0, 0, 0);
}

long lc_open(const char* path, int flags) {
    return (long)__lc_syscall6(LC_OPEN, (uint32_t)path, (uint32_t)flags, 0, 0, 0, 0);
}

long lc_close(int fd) {
    return (long)__lc_syscall6(LC_CLOSE, (uint32_t)fd, 0, 0, 0, 0, 0);
}

long lc_set_thread_area(void* base) {
    return (long)__lc_syscall6(LC_SET_THREAD_AREA, (uint32_t)base, 0, 0, 0, 0, 0);
}

long lc_writev(int fd, const struct lc_iovec* iov, int iovcnt) {
    return (long)__lc_syscall6(LC_WRITEV, (uint32_t)fd, (uint32_t)iov, (uint32_t)iovcnt, 0, 0, 0);
}

long lc_mmap(void* addr, uint32_t len, int prot, int flags, int fd, uint32_t off) {
    return (long)__lc_syscall6(LC_MMAP, (uint32_t)addr, len, (uint32_t)prot,
                               (uint32_t)flags, (uint32_t)fd, off);
}

void __lc_terminate(int status) {
    __lc_syscall6(LC_EXIT, (uint32_t)status, 0, 0, 0, 0, 0);
    for (;;) { }
}

uint32_t lc_strlen(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return (uint32_t)(p - s);
}

int lc_strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

void lc_puts(const char* s) {
    lc_write(1, s, lc_strlen(s));
}

void lc_putc(char c) {
    (void)lc_write(1, &c, 1);
}

void lc_putuint(uint32_t v) {
    char buf[12]; int n = 0;
    do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) lc_putc(buf[--n]);
}

void lc_puthex(uint32_t v) {
    static const char h[] = "0123456789abcdef";
    lc_puts("0x");
    for (int i = 28; i >= 0; i -= 4) lc_putc(h[(v >> i) & 0xF]);
}

unsigned long lc_auxv_get(int tag) {
    uint32_t* p = __lc_auxv;
    if (!p) return 0;
    while (p[0] != 0) {
        if ((int)p[0] == tag) return p[1];
        p += 2;
    }
    return 0;
}