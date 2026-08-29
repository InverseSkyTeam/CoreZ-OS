#include "./linux_compat.h"
#include "../device/ioqueue.h"
#include "../device/keyboard.h"
#include "../fs/fs.h"
#include "../include/asmFunc.h"
#include "../include/signal.h"
#include "../initer/gdt/gdt.h"
#include "../initer/io/io.h"
#include "../lib/user/syscall.h"
#include "../shell/pipe.h"
#include "../thread/thread.h"
#include "../userprog/process.h"
#include "../userprog/wait_exit.h"
#include "./file_syscall.h"
#include "./futex.h"
#include "./mmap.h"

uint32_t sys_brk(uint32_t addr);
int32_t sys_clock_gettime(int32_t clk_id, struct timespec *tp);
int32_t sys_gettimeofday(struct timeval *tv, void *tz);
int32_t sys_nanosleep(const struct timespec *req, struct timespec *rem);

static void lc_seterrno(struct task_struct *cur, int32_t val) {
    cur->errno = val;
    if (cur->tls_base != 0) {
        *(volatile int32_t *)cur->tls_base = val;
    }
}

static int32_t compat_write(int32_t fd, const void *buf, uint32_t count) {
    if (fd < 0)
        return -1;
    if (is_pipe(fd))
        return (int32_t)pipe_write(fd, buf, count);
    const char *s = (const char *)buf;
    for (uint32_t i = 0; i < count; i++) {
        console_putc(s[i]);
    }
    return (int32_t)count;
}

static int32_t compat_read(int32_t fd, void *buf, uint32_t count) {
    if (fd == 0) {
        uint8_t *p = (uint8_t *)buf;
        uint32_t got = 0;
        asm_cli();
        while (got < count) {
            char c = ioq_getchar(&keyboard_ioq);
            asm_sti();
            p[got++] = (uint8_t)c;
            if (c == '\n' || c == '\r')
                break;
            asm_cli();
        }
        asm_sti();
        return (int32_t)got;
    }
    if (is_pipe(fd))
        return (int32_t)pipe_read(fd, buf, count);
    if (fd >= 0 && fd < 3)
        return -1;
    return (int32_t)read_file(fd, buf, count);
}

static uint32_t compat_brk_alloc(struct task_struct *cur, uint32_t len) {
    uint32_t curbrk = sys_brk(0);
    uint32_t want = curbrk + ((len + 0xFFF) & ~0xFFF);
    if (sys_brk(want) != want)
        return (uint32_t)-1;
    return curbrk;
}

static int32_t compat_set_thread_area(struct Registers *r,
                                      struct task_struct *cur, uint32_t base) {
    if (base == 0)
        return -1;
    cur->tls_base = base;
    cur->tls_selector = SELECTOR_TLS;
    tls_desc_set_base(base);
    lc_seterrno(cur, 0);
    return 0;
}

static int32_t sys_compat_writev(int32_t fd, struct LINUX_IOVEC *iov,
                                 int32_t iovcnt) {
    if (iovcnt < 0)
        return -1;
    uint32_t total = 0;
    for (int32_t i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0)
            continue;
        int32_t n = compat_write(fd, (const void *)iov[i].iov_base, iov[i].iov_len);
        if (n < 0)
            return -1;
        total += (uint32_t)n;
    }
    return (int32_t)total;
}

uint32_t linux_compat_handler(struct Registers *r) {
    struct task_struct *cur = current;
    uint32_t nr = r->eax;
    uint32_t ret = (uint32_t)-1;

    /* custom libc syscalls (LC_*, int 0x80 ABI) */
    if (nr >= COMPAT_SYSCALL_BASE) {
        /* int 0x80 ABI: rbx rcx rdx rsi rdi rbp = arg1..arg6 */
        uint32_t a = r->rbx;
        uint32_t b = r->rcx;
        uint32_t c = r->rdx;
        uint32_t d = r->rsi;
        uint32_t e = r->rdi;
        uint32_t f = r->rbp;

        switch (nr - COMPAT_SYSCALL_BASE) {
        case 0: /* LC_PID */
            ret = cur->pid;
            break;
        case 1: /* LC_WRITE */ {
            int32_t n = compat_write((int32_t)a, (const void *)b, c);
            lc_seterrno(cur, n < 0 ? -n : 0);
            ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
            break;
        }
        case 2: /* LC_READ */ {
            int32_t n = compat_read((int32_t)a, (void *)b, c);
            lc_seterrno(cur, n < 0 ? -n : 0);
            ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
            break;
        }
        case 3: /* LC_EXIT */
            sys_exit((int32_t)a);
            ret = 0;
            break;
        case 4: /* LC_BRK */ {
            uint32_t rr = sys_brk(a);
            lc_seterrno(cur, 0);
            ret = rr;
            break;
        }
        case 5: /* LC_OPEN */ {
            int32_t fd = open_file((const char *)a, (uint8_t)b);
            lc_seterrno(cur, fd < 0 ? -fd : 0);
            ret = fd < 0 ? (uint32_t)-1 : (uint32_t)fd;
            break;
        }
        case 6: /* LC_CLOSE */ {
            int32_t rr = close_file((int32_t)a);
            lc_seterrno(cur, rr < 0 ? -rr : 0);
            ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
            break;
        }
        case 7: /* LC_MMAP */ {
            if (a != 0) {
                ret = a;
            } else {
                ret = compat_brk_alloc(cur, b);
            }
            lc_seterrno(cur, (int32_t)ret == (uint32_t)-1 ? 1 : 0);
            break;
        }
        case 8: /* LC_SET_THREAD_AREA */ {
            int32_t rr = compat_set_thread_area(r, cur, a);
            lc_seterrno(cur, rr < 0 ? -rr : 0);
            ret = rr < 0 ? (uint32_t)-1 : 0;
            break;
        }
        case 9: /* LC_WRITEV */ {
            int32_t n =
                sys_compat_writev((int32_t)a, (struct LINUX_IOVEC *)b, (int32_t)c);
            lc_seterrno(cur, n < 0 ? -n : 0);
            ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
            break;
        }
        default:
            lc_seterrno(cur, 38); /* ENOSYS */
            ret = (uint32_t)-1;
            break;
        }
        return ret;
    }

    /* Linux x86_64 ABI: rdi rsi rdx r10 r8 r9 = arg1..arg6 */
    uint32_t a = r->rdi;
    uint32_t b = r->rsi;
    uint32_t c = r->rdx;
    uint32_t d = r->r10;
    uint32_t e = r->r8;
    uint32_t f = r->r9;

    switch (nr) {
    case SYS_LINUX_write: {
        int32_t n = compat_write((int32_t)a, (const void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_read: {
        int32_t n = compat_read((int32_t)a, (void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_open: {
        int32_t fd = open_file((const char *)a, (uint8_t)b);
        lc_seterrno(cur, fd < 0 ? -fd : 0);
        ret = fd < 0 ? (uint32_t)-1 : (uint32_t)fd;
        break;
    }
    case SYS_LINUX_close: {
        int32_t rr = close_file((int32_t)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_exit:
        sys_exit((int32_t)a);
        ret = 0;
        break;
    case SYS_LINUX_exit_group:
        sys_exit((int32_t)a);
        for (;;) {
        }
        break;
    case SYS_LINUX_brk: {
        uint32_t rr = sys_brk(a);
        lc_seterrno(cur, 0);
        ret = rr;
        break;
    }
    case SYS_LINUX_mmap: {
        if (a != 0) {
            ret = a;
        } else {
            ret = compat_brk_alloc(cur, b);
        }
        lc_seterrno(cur, (int32_t)ret == (uint32_t)-1 ? 1 : 0);
        break;
    }
    case SYS_LINUX_munmap: {
        lc_seterrno(cur, 0);
        ret = 0;
        break;
    }
    case SYS_LINUX_set_thread_area: {
        int32_t rr = compat_set_thread_area(r, cur, a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_set_tid_address: {
        cur->tls_base = a;
        cur->tls_selector = SELECTOR_TLS;
        tls_desc_set_base(a);
        *(volatile int32_t *)a = (int32_t)cur->pid;
        lc_seterrno(cur, 0);
        ret = (uint32_t)cur->pid;
        break;
    }
    case SYS_LINUX_writev: {
        int32_t n =
            sys_compat_writev((int32_t)a, (struct LINUX_IOVEC *)b, (int32_t)c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_getpid:
        ret = cur->pid;
        break;
    case SYS_LINUX_getuid:
        ret = 0;
        break;
    case SYS_LINUX_getgid:
        ret = 0;
        break;
    case SYS_LINUX_geteuid:
        ret = 0;
        break;
    case SYS_LINUX_getegid:
        ret = 0;
        break;
    case SYS_LINUX_getppid:
        ret = cur->parent_pid >= 0 ? (uint32_t)cur->parent_pid : 0;
        break;
    case SYS_LINUX_fstat: {
        int32_t rr = sys_fstat((int32_t)a, (void *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_stat: {
        int32_t rr = sys_stat((const char *)a, (struct stat *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_lstat: {
        int32_t rr = sys_stat((const char *)a, (struct stat *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_lseek: {
        int32_t rr = sys_lseek((int32_t)a, (int32_t)b, (uint8_t)c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_fcntl: {
        int32_t rr = sys_fcntl((int32_t)a, (int32_t)b, c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_readlink: {
        int32_t rr = sys_readlink((const char *)a, (char *)b, c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_chdir: {
        int32_t rr = sys_chdir((const char *)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_getcwd: {
        char *rr = sys_getcwd((char *)a, b);
        lc_seterrno(cur, rr == NULL ? 2 : 0);
        ret = rr == NULL ? (uint32_t)-1 : (uint32_t)a;
        break;
    }
    case SYS_LINUX_mkdir: {
        int32_t rr = sys_mkdir((const char *)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rmdir: {
        int32_t rr = sys_rmdir((const char *)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_unlink: {
        int rr = sys_unlink((const char *)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rename: {
        int32_t rr = sys_rename((const char *)a, (const char *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_chmod: {
        int32_t rr = sys_chmod((const char *)a, b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_access: {
        int32_t rr = sys_access((const char *)a, (int32_t)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_kill: {
        int rr = sys_kill((int)a, (int)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_futex: {
        int32_t rr = sys_futex(a, b, c, d);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_gettimeofday: {
        int32_t rr = sys_gettimeofday((struct timeval *)a, (void *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_nanosleep: {
        int32_t rr =
            sys_nanosleep((const struct timespec *)a, (struct timespec *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_clock_gettime: {
        int32_t rr = sys_clock_gettime((int32_t)a, (struct timespec *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_mprotect: {
        int32_t rr = sys_mprotect(a, b, c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rt_sigaction: {
        if (a == 0) {
            lc_seterrno(cur, 22);
            ret = (uint32_t)-1;
        } else {
            int rr = sys_sigaction((int)a, (const struct sigaction *)b,
                                   (struct sigaction *)c);
            lc_seterrno(cur, rr < 0 ? -rr : 0);
            ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        }
        break;
    }
    case SYS_LINUX_rt_sigprocmask: {
        if (a == 0) {
            lc_seterrno(cur, 22);
            ret = (uint32_t)-1;
        } else {
            int rr =
                sys_sigprocmask((int)a, (const sigset_t *)b, (sigset_t *)c);
            lc_seterrno(cur, rr < 0 ? -rr : 0);
            ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        }
        break;
    }
    case SYS_LINUX_sched_yield:
        ret = 0;
        break;
    default:
        lc_seterrno(cur, 38); /* ENOSYS */
        ret = (uint32_t)-1;
        break;
    }
    return ret;
}
