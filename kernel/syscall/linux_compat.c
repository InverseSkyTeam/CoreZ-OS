#include "kernel/syscall/linux_compat.h"
#include "kernel/mm/access.h"
#include "arch/x86/interrupt/interrupt.h"
#include "drivers/char/console/io.h"
#include "drivers/char/keyboard.h"
#include "drivers/char/tty.h"
#include "kernel/asmFunc.h"
#include "kernel/fs/dir.h"
#include "kernel/fs/ext2.h"
#include "kernel/fs/file.h"
#include "kernel/fs/fs.h"
#include "kernel/init/gdt/gdt.h"
#include "kernel/init/pit/pit.h"
#include "kernel/sched/thread.h"
#include "kernel/shell/pipe.h"
#include "kernel/signal.h"
#include "kernel/syscall/file_syscall.h"
#include "kernel/syscall/futex.h"
#include "kernel/syscall/mmap.h"
#include "kernel/userprog/process.h"
#include "kernel/userprog/wait_exit.h"
#include "lib/str/str.h"
#include "libc/user/syscall.h"

uint32_t sys_brk(uint32_t addr);
int32_t sys_clock_gettime(int32_t clk_id, struct timespec *tp);
int32_t sys_gettimeofday(struct timeval *tv, void *tz);
int32_t sys_nanosleep(const struct timespec *req, struct timespec *rem);
int32_t sys_wait(int32_t *status);
int32_t sys_sigaction(int sig, const struct sigaction *act,
                      struct sigaction *old);
int32_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
uint64_t sys_sigreturn(struct Registers *r);

static int32_t compat_read(int32_t fd, void *buf, uint32_t count);
static int32_t compat_write(int32_t fd, const void *buf, uint32_t count);

static int64_t lc_uptime_ms(void) {
    return (int64_t)tick * (1000 / PIT_HZ);
}

static int32_t compat_getdents64(int32_t fd, void *dirp, uint32_t count) {
    if (dirp == NULL || fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC)
        return -LINUX_EBADF;
    uint32_t gfd = fd_local2global((uint32_t)fd);
    struct file *pf = file_get(gfd);
    if (pf == NULL || pf->fd_inode == NULL)
        return -LINUX_EBADF;
    uint32_t pos = pf->fd_pos;
    uint32_t emitted = pos;
    uint32_t written = 0;
    struct dir_entry de;
    for (;;) {
        if (ext2_dir_next(pf->fd_inode, &pos, &de) != 0)
            break;
        uint32_t nl = strlen(de.filename);
        uint16_t reclen = (uint16_t)((19u + nl + 1u + 7u) & ~7u);
        if (written + reclen > count) {
            if (written == 0)
                return -LINUX_EINVAL;
            break;
        }
        struct LINUX_DIRENT64 *d =
            (struct LINUX_DIRENT64 *)((uint8_t *)dirp + written);
        d->d_ino = de.i_no;
        d->d_off = (int64_t)pos;
        d->d_reclen = reclen;
        d->d_type = (de.f_type == FT_DIRECTORY) ? LINUX_DT_DIR : LINUX_DT_REG;
        memcpy(d->d_name, de.filename, nl + 1);
        written += reclen;
        emitted = pos;
    }
    pf->fd_pos = emitted;
    return (int32_t)written;
}

static int32_t compat_ioctl(int32_t fd, uint32_t cmd, uint64_t arg) {
    if (fd >= 0 && fd <= 2) {
        uint32_t native = 0;
        switch (cmd) {
        case LINUX_TCGETS:
            native = TTY_IOCTL_TCGETS;
            break;
        case LINUX_TCSETS:
            native = TTY_IOCTL_TCSETS;
            break;
        case LINUX_TIOCGWINSZ:
            native = TTY_IOCTL_TIOCGWINSZ;
            break;
        case LINUX_FIONREAD:
            native = TTY_IOCTL_FIONREAD;
            break;
        default:
            return -LINUX_ENOTTY;
        }
        int32_t rc = TTY.ioctl(native, arg);
        return rc < 0 ? -LINUX_ENOTTY : rc;
    }
    return -LINUX_ENOTTY;
}

static int32_t compat_setpgid(uint32_t pid, uint32_t pgid) {
    if (pid >= MAX_TASKS || pgid >= MAX_TASKS)
        return -LINUX_EINVAL;
    struct task_struct *t = pid2thread((int32_t)pid);
    if (t == NULL || t->status == TASK_DIED)
        return -LINUX_ESRCH;
    uint32_t want = pgid ? pgid : pid;
    if (want >= MAX_TASKS)
        return -LINUX_EINVAL;
    struct task_struct *g = pid2thread((int32_t)want);
    if (g == NULL)
        return -LINUX_EPERM;
    t->pid = want;
    return 0;
}

static int32_t compat_getpgid(uint32_t pid) {
    if (pid >= MAX_TASKS)
        return -LINUX_EINVAL;
    struct task_struct *t = pid2thread((int32_t)pid);
    if (t == NULL)
        return -LINUX_ESRCH;
    return (int32_t)t->pid;
}

static int32_t compat_readv(int32_t fd, struct LINUX_IOVEC *iov,
                            int32_t iovcnt) {
    if (iovcnt < 0 || iovcnt > 16)
        return -LINUX_EINVAL;
    int32_t total = 0;
    for (int32_t i = 0; i < iovcnt; i++) {
        struct LINUX_IOVEC v;
        memcpy(&v, (const void *)(uintptr_t)&iov[i], sizeof(v));
        if (v.iov_len == 0)
            continue;
        int32_t n = compat_read(fd, v.iov_base, (uint32_t)v.iov_len);
        if (n < 0)
            return total > 0 ? total : n;
        total += n;
        if ((uint32_t)n < v.iov_len)
            break;
    }
    return total;
}

static int32_t compat_wait4(int32_t *status_out) {
    int32_t st = 0;
    int32_t pid = sys_wait(&st);
    if (pid < 0)
        return -LINUX_ECHILD;
    if (status_out) {
        int32_t s8 = st & 0xff;
        uint32_t lst;
        if (s8 >= 128)
            lst = (uint32_t)(s8 - 128);
        else
            lst = (uint32_t)s8 << 8;
        *(int32_t *)status_out = (int32_t)lst;
    }
    return pid;
}

static int32_t compat_uname(void *buf) {
    struct LINUX_UTSNAME u;
    memset(&u, 0, sizeof(u));
    memcpy(u.sysname, "Linux", 6);
    memcpy(u.nodename, "corez", 6);
    memcpy(u.release, "5.10.0-corez", 13);
    memcpy(u.version, "#1 NitianOS SMP", 16);
    memcpy(u.machine, "x86_64", 7);
    memcpy(u.domainname, "(none)", 7);
    memcpy(buf, &u, sizeof(u));
    return 0;
}

static int32_t compat_sysinfo(void *buf) {
    struct LINUX_SYSINFO si;
    memset(&si, 0, sizeof(si));
    si.uptime = (int64_t)(tick / PIT_HZ);
    si.totalram = (uint64_t)kernel_pool.pool_size;
    uint32_t used = 0;
    for (uint32_t i = 0; i < kernel_pool.pool_bitmap.btmp_bytes_len * 8; i++) {
        if (bitmap_scan_test(&kernel_pool.pool_bitmap, i))
            used++;
    }
    si.freeram = si.totalram - (uint64_t)used * PAGE_SIZE;
    si.mem_unit = 1;
    si.procs = 1;
    memcpy(buf, &si, sizeof(si));
    return 0;
}

static int32_t compat_times(void *buf) {
    if (buf) {
        struct LINUX_TMS t;
        memset(&t, 0, sizeof(t));
        t.utime = (int64_t)current->elapsed_ticks;
        memcpy(buf, &t, sizeof(t));
    }
    return (int32_t)tick;
}

static int32_t compat_ftruncate(int32_t fd, int32_t length) {
    (void)length;
    if (fd < 3 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC)
        return -LINUX_EINVAL;
    uint32_t gfd = fd_local2global((uint32_t)fd);
    struct file *pf = file_get(gfd);
    if (pf == NULL || pf->fd_inode == NULL || pf->fd_flag == PIPE_FLAG)
        return -LINUX_EINVAL;
    ext2_truncate_inode(pf->fd_inode);
    ext2_write_inode(pf->fd_inode->i_no, pf->fd_inode);
    return 0;
}

static int32_t compat_truncate(const char *path, int32_t length) {
    (void)length;
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(path, &ino, &is_dir) || is_dir)
        return -LINUX_ENOENT;
    struct inode *obj = inode_open(cur_part, ino);
    if (obj == NULL)
        return -LINUX_ENOENT;
    ext2_truncate_inode(obj);
    ext2_write_inode(ino, obj);
    inode_close(obj);
    return 0;
}

static int32_t compat_rename(const char *oldpath, const char *newpath) {
    uint32_t ino = 0;
    int is_dir = 0;
    if (ext2_lookup(oldpath, &ino, &is_dir))
        return -LINUX_ENOENT;
    uint32_t nino = 0;
    int ndir = 0;
    if (ext2_lookup(newpath, &nino, &ndir) == 0)
        return -LINUX_EEXIST;
    const char *oslash = strrchr(oldpath, '/');
    const char *nslash = strrchr(newpath, '/');
    if (oslash == NULL || nslash == NULL)
        return -LINUX_ENOENT;
    char olddir[256];
    char newdir[256];
    uint32_t ol = (uint32_t)(oslash - oldpath);
    uint32_t nl = (uint32_t)(nslash - newpath);
    if (ol == 0 || nl == 0 || ol >= 256 || nl >= 256)
        return -LINUX_EINVAL;
    memcpy(olddir, oldpath, ol);
    olddir[ol] = 0;
    memcpy(newdir, newpath, nl);
    newdir[nl] = 0;
    uint32_t opino = 0;
    int opdir = 0;
    uint32_t npino = 0;
    int npdir = 0;
    if (ext2_lookup(ol == 1 ? "/" : olddir, &opino, &opdir) || !opdir)
        return -LINUX_ENOENT;
    if (ext2_lookup(nl == 1 ? "/" : newdir, &npino, &npdir) || !npdir)
        return -LINUX_ENOENT;
    struct inode *np = inode_open(cur_part, npino);
    if (np == NULL)
        return -LINUX_ENOENT;
    int rc = ext2_add_entry(np, ino, nslash + 1, is_dir);
    inode_close(np);
    if (rc)
        return -LINUX_ENOSPC;
    struct inode *op = inode_open(cur_part, opino);
    if (op == NULL)
        return -LINUX_ENOENT;
    rc = ext2_remove_entry(op, oslash + 1);
    inode_close(op);
    if (rc)
        return -LINUX_ENOENT;
    return 0;
}

static void lc_seterrno(struct task_struct *cur, int32_t val) {
    cur->errno = val;
    if (cur->tls_selector == SELECTOR_TLS && cur->tls_base != 0) {
        *(volatile int32_t *)cur->tls_base = val;
    }
}

static int32_t compat_write(int32_t fd, const void *buf, uint32_t count) {
    if (fd < 0)
        return -1;
    if (is_pipe(fd))
        return (int32_t)pipe_write(fd, buf, count);
    if (fd == 1 || fd == 2)
        return TTY.write((const char *)buf, count);
    const char *s = (const char *)buf;
    for (uint32_t i = 0; i < count; i++) {
        console_putc(s[i]);
    }
    return (int32_t)count;
}

static int32_t compat_read(int32_t fd, void *buf, uint32_t count) {
    if (fd == 0)
        return TTY.read((char *)buf, count);
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
    if (base == 0 || !user_range_writable(base, sizeof(int32_t)))
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
        int32_t n =
            compat_write(fd, (const void *)iov[i].iov_base, iov[i].iov_len);
        if (n < 0)
            return -1;
        total += (uint32_t)n;
    }
    return (int32_t)total;
}

static int ugate(struct Registers *r, uint64_t ptr, uint32_t len, int wr) {
    return (r->cs & 3) != 3 ||
           access_ok((const void *)(uintptr_t)ptr, len, wr);
}
static int ustr(struct Registers *r, char *dst, uint64_t ptr) {
    return (r->cs & 3) != 3 ||
           copy_str_from_user(dst, (const char *)(uintptr_t)ptr,
                              MAX_PATH_LEN) == 0;
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
            if (!ugate(r, b, c, 0)) {
                lc_seterrno(cur, LINUX_EFAULT);
                ret = (uint32_t)-1;
                break;
            }
            int32_t n = compat_write((int32_t)a, (const void *)b, c);
            lc_seterrno(cur, n < 0 ? -n : 0);
            ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
            break;
        }
        case 2: /* LC_READ */ {
            if (!ugate(r, b, c, 1)) {
                lc_seterrno(cur, LINUX_EFAULT);
                ret = (uint32_t)-1;
                break;
            }
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
            char kpath[MAX_PATH_LEN];
            if (!ustr(r, kpath, a)) {
                lc_seterrno(cur, LINUX_EFAULT);
                ret = (uint32_t)-1;
                break;
            }
            int32_t fd = open_file(kpath, (uint8_t)b);
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
            int32_t total = 0;
            int32_t err = 0;
            int32_t i;
            if (b == 0 && c > 0) {
                err = 14;
            } else if (c > 1024 || !ugate(r, b, c * 8u, 0)) {
                err = 14;
            } else {
                for (i = 0; i < (int32_t)c; i++) {
                    uint32_t pair[2];
                    memcpy(pair,
                           (const void *)(uintptr_t)(b + (uint32_t)i * 8u),
                           sizeof(pair));
                    if (pair[1] == 0)
                        continue;
                    if (!ugate(r, pair[0], pair[1], 0)) {
                        err = 14;
                        break;
                    }
                    int32_t n = compat_write(
                        (int32_t)a, (const void *)(uintptr_t)pair[0], pair[1]);
                    if (n < 0) {
                        err = -n;
                        break;
                    }
                    total += n;
                    if ((uint32_t)n < pair[1])
                        break;
                }
            }
            lc_seterrno(cur, err);
            ret = err ? (uint32_t)-1 : (uint32_t)total;
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
        if (!ugate(r, b, c, 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t n = compat_write((int32_t)a, (const void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_read: {
        if (!ugate(r, b, c, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t n = compat_read((int32_t)a, (void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_open: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t fd = open_file(kpath, (uint8_t)b);
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
        if (a)
            *(volatile int32_t *)a = (int32_t)cur->pid;
        lc_seterrno(cur, 0);
        ret = (uint32_t)cur->pid;
        break;
    }
    case SYS_LINUX_writev: {
        if (!ugate(r, b, (uint32_t)c * 8u, 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
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
        if (!ugate(r, b, sizeof(struct stat), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_fstat((int32_t)a, (void *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_stat: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a) || !ugate(r, b, sizeof(struct stat), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_stat(kpath, (struct stat *)b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_lstat: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a) || !ugate(r, b, sizeof(struct stat), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_stat(kpath, (struct stat *)b);
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
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a) || !ugate(r, b, c, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_readlink(kpath, (char *)b, c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_chdir: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_chdir(kpath);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_getcwd: {
        if (!ugate(r, a, b, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        char *rr = sys_getcwd((char *)a, b);
        lc_seterrno(cur, rr == NULL ? 2 : 0);
        ret = rr == NULL ? (uint32_t)-1 : (uint32_t)a;
        break;
    }
    case SYS_LINUX_mkdir: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_mkdir(kpath);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rmdir: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_rmdir(kpath);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_unlink: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_unlink(kpath);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rename: {
        char kfrom[MAX_PATH_LEN];
        char kto[MAX_PATH_LEN];
        if (!ustr(r, kfrom, a) || !ustr(r, kto, b)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_rename(kfrom, kto);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_chmod: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_chmod(kpath, b);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_access: {
        char kpath[MAX_PATH_LEN];
        if (!ustr(r, kpath, a)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_access(kpath, (int32_t)b);
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
        if (!ugate(r, a, 4, 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t rr = sys_futex(a, b, c, d);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_gettimeofday: {
        struct LINUX_TIMEVAL tv;
        tv.tv_sec = (int64_t)(tick / PIT_HZ);
        tv.tv_usec = (int64_t)((tick % PIT_HZ) * (1000000 / PIT_HZ));
        if (a && !ugate(r, a, sizeof(tv), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b && !ugate(r, b, 16, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (a)
            memcpy((void *)(uintptr_t)a, &tv, sizeof(tv));
        if (b)
            memset((void *)(uintptr_t)b, 0, 16);
        lc_seterrno(cur, 0);
        ret = 0;
        break;
    }
    case SYS_LINUX_nanosleep: {
        struct LINUX_TIMESPEC req;
        memset(&req, 0, sizeof(req));
        if (b && !ugate(r, b, sizeof(req), 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b)
            memcpy(&req, (const void *)(uintptr_t)b, sizeof(req));
        int64_t ms = req.tv_sec * 1000 + req.tv_nsec / 1000000;
        mtime_sleep(ms < 0 ? 0 : (uint32_t)ms);
        if (d && !ugate(r, d, sizeof(struct LINUX_TIMESPEC), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (d) {
            struct LINUX_TIMESPEC rem;
            memset(&rem, 0, sizeof(rem));
            memcpy((void *)(uintptr_t)d, &rem, sizeof(rem));
        }
        lc_seterrno(cur, 0);
        ret = 0;
        break;
    }
    case SYS_LINUX_clock_gettime: {
        struct LINUX_TIMESPEC ts;
        ts.tv_sec = (int64_t)(tick / PIT_HZ);
        ts.tv_nsec = (int64_t)((tick % PIT_HZ) * (1000000000 / PIT_HZ));
        if (b && !ugate(r, b, sizeof(ts), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b)
            memcpy((void *)(uintptr_t)b, &ts, sizeof(ts));
        lc_seterrno(cur, 0);
        ret = 0;
        break;
    }
    case SYS_LINUX_clock_getres: {
        if (b && !ugate(r, b, sizeof(struct LINUX_TIMESPEC), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b) {
            struct LINUX_TIMESPEC res;
            res.tv_sec = 0;
            res.tv_nsec = 1000000000 / PIT_HZ;
            memcpy((void *)(uintptr_t)b, &res, sizeof(res));
        }
        lc_seterrno(cur, 0);
        ret = 0;
        break;
    }
    case SYS_LINUX_mprotect: {
        int32_t rr = sys_mprotect(a, b, c);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case SYS_LINUX_rt_sigaction: {
        int32_t sig = (int32_t)a;
        if (sig <= 0 || sig >= 32) {
            lc_seterrno(cur, LINUX_EINVAL);
            ret = (uint32_t)-1;
            break;
        }
        struct LINUX_SIGACTION lsa;
        memset(&lsa, 0, sizeof(lsa));
        if (b && !ugate(r, b, sizeof(lsa), 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b)
            memcpy(&lsa, (const void *)(uintptr_t)b, sizeof(lsa));
        struct sigaction nat;
        memset(&nat, 0, sizeof(nat));
        nat.sa_handler = (void (*)(int))(uintptr_t)lsa.sa_handler;
        nat.sa_mask = (uint32_t)lsa.sa_mask;
        nat.sa_flags = (uint32_t)lsa.sa_flags;
        nat.sa_restorer = (void *)(uintptr_t)lsa.sa_restorer;
        struct sigaction oldnat;
        memset(&oldnat, 0, sizeof(oldnat));
        int rr = sys_sigaction(sig, b ? &nat : NULL, &oldnat);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        if (rr == 0 && d && !ugate(r, d, sizeof(struct LINUX_SIGACTION), 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (rr == 0 && d) {
            struct LINUX_SIGACTION oldl;
            memset(&oldl, 0, sizeof(oldl));
            oldl.sa_handler = (uint64_t)(uintptr_t)oldnat.sa_handler;
            oldl.sa_flags = oldnat.sa_flags;
            oldl.sa_restorer = (uint64_t)(uintptr_t)oldnat.sa_restorer;
            oldl.sa_mask = oldnat.sa_mask;
            memcpy((void *)(uintptr_t)d, &oldl, sizeof(oldl));
        }
        ret = rr < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_rt_sigprocmask: {
        uint32_t how = a;
        sigset_t kset = 0;
        if (b && !ugate(r, b, 8, 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (c && !ugate(r, c, 8, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        if (b) {
            uint8_t in[8];
            memset(in, 0, sizeof(in));
            memcpy(in, (const void *)(uintptr_t)b, 8);
            memcpy(&kset, in, sizeof(kset));
        }
        sigset_t oset = 0;
        int32_t rr =
            sys_sigprocmask((int32_t)how, b ? &kset : NULL, c ? &oset : NULL);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        if (c) {
            uint8_t out[8];
            memset(out, 0, sizeof(out));
            memcpy(out, &oset, sizeof(oset));
            memcpy((void *)(uintptr_t)c, out, 8);
        }
        ret = rr < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_getdents64: {
        if (!ugate(r, b, c, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t n = compat_getdents64((int32_t)a, (void *)(uintptr_t)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_ioctl: {
        int32_t n = compat_ioctl((int32_t)a, (uint32_t)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_readv: {
        if (!ugate(r, b, (uint32_t)c * 8u, 0)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t n = compat_readv((int32_t)a, (struct LINUX_IOVEC *)(uintptr_t)b,
                                 (int32_t)c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_wait4: {
        if (d && !ugate(r, d, 4, 1)) {
            lc_seterrno(cur, LINUX_EFAULT);
            ret = (uint32_t)-1;
            break;
        }
        int32_t pid = compat_wait4((int32_t *)d);
        lc_seterrno(cur, pid < 0 ? -pid : 0);
        ret = pid < 0 ? (uint32_t)-1 : (uint32_t)pid;
        break;
    }
    case SYS_LINUX_uname: {
        int32_t n = compat_uname((void *)(uintptr_t)a);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_sysinfo: {
        int32_t n = compat_sysinfo((void *)(uintptr_t)a);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_times: {
        int32_t n = compat_times((void *)(uintptr_t)a);
        lc_seterrno(cur, 0);
        ret = (uint32_t)n;
        break;
    }
    case SYS_LINUX_ftruncate: {
        int32_t n = compat_ftruncate((int32_t)a, (int32_t)c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_rt_sigreturn:
        ret = (uint32_t)sys_sigreturn(r);
        break;
    case SYS_LINUX_setpgid: {
        int32_t n = compat_setpgid(a, b);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case SYS_LINUX_getpgid: {
        int32_t n = compat_getpgid(a);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case SYS_LINUX_arch_prctl: {
        const uint64_t MSR_FS_BASE = 0xC0000100;
        const uint64_t MSR_GS_BASE = 0xC0000101;
        uint32_t code = a;
        uint64_t base = b;
        if (code == 0x1002u) {
            cur->tls_base = (uint32_t)base;
            asm_wrmsr(MSR_FS_BASE, base);
            lc_seterrno(cur, 0);
            ret = 0;
        } else if (code == 0x1001u) {
            cur->tls_base = (uint32_t)base;
            asm_wrmsr(MSR_GS_BASE, base);
            lc_seterrno(cur, 0);
            ret = 0;
        } else if (code == 0x1003u) {
            ret = (uint32_t)asm_rdmsr(MSR_FS_BASE);
        } else if (code == 0x1004u) {
            ret = (uint32_t)asm_rdmsr(MSR_GS_BASE);
        } else {
            lc_seterrno(cur, LINUX_EINVAL);
            ret = (uint32_t)-1;
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
