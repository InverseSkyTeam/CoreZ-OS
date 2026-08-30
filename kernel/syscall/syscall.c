#include "kernel/syscall/syscall.h"
#include "arch/x86/interrupt/interrupt.h"
#include "drivers/char/console/io.h"
#include "drivers/char/ioqueue.h"
#include "drivers/char/keyboard.h"
#include "drivers/char/tty.h"
#include "drivers/net/net.h"
#include "drivers/net/socket.h"
#include "kernel/asmFunc.h"
#include "kernel/assert.h"
#include "kernel/fs/file.h"
#include "kernel/fs/fs.h"
#include "kernel/gui/gui.h"
#include "kernel/init/acpi/acpi.h"
#include "kernel/init/gdt/gdt.h"
#include "kernel/init/pit/pit.h"
#include "kernel/mm/access.h"
#include "kernel/sched/thread.h"
#include "kernel/shell/pipe.h"
#include "kernel/syscall/file_syscall.h"
#include "kernel/syscall/futex.h"
#include "kernel/syscall/linux_compat.h"
#include "kernel/syscall/mmap.h"
#include "kernel/userprog/clone.h"
#include "kernel/userprog/exec.h"
#include "kernel/userprog/fork.h"
#include "kernel/userprog/process.h"
#include "kernel/userprog/wait_exit.h"
#include "lib/str/str.h"
#include "libc/user/syscall.h"
static uint32_t sys_getpid(void) {
    return current->pid;
}
int32_t sys_clock_gettime(int32_t clk_id, struct timespec *tp) {
    if (tp == NULL) {
        return -1;
    }
    (void)clk_id;
    memset(tp, 0, sizeof(*tp));
    tp->tv_sec = (int32_t)(tick / PIT_HZ);
    tp->tv_nsec = (int32_t)((tick % PIT_HZ) * (1000u * 1000u * 1000u / PIT_HZ));
    return 0;
}
int32_t sys_gettimeofday(struct timeval *tv, void *tz) {
    if (tv == NULL) {
        return -1;
    }
    (void)tz;
    memset(tv, 0, sizeof(*tv));
    tv->tv_sec = (int32_t)(tick / PIT_HZ);
    tv->tv_usec = (int32_t)((tick % PIT_HZ) * (1000u * 1000u / PIT_HZ));
    return 0;
}
int32_t sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (req == NULL || req->tv_sec < 0 || req->tv_nsec < 0) {
        return -1;
    }
    uint32_t sec = (uint32_t)req->tv_sec;
    uint32_t ms;
    if (sec > 0x1fffff) {
        ms = 0x7fffffffU;
    } else {
        ms = sec * 1000u;
    }
    ms += (uint32_t)req->tv_nsec / 1000000u;
    if (ms > 0x7fffffffU) {
        ms = 0x7fffffffU;
    }
    mtime_sleep(ms);
    if (rem != NULL) {
        memset(rem, 0, sizeof(*rem));
    }
    return 0;
}
static uint32_t sys_getuid(void) {
    return 0;
}
static uint32_t sys_getgid(void) {
    return 0;
}
static uint32_t sys_geteuid(void) {
    return 0;
}
static uint32_t sys_getegid(void) {
    return 0;
}
static void sys_exit_group(int32_t status) {
    sys_exit(status);
    for (;;) {
    }
}
static uint32_t sys_shutdown(void) {
    kprintf("[shutdown] shutting down system...\n");
    acpi_shutdown();
    return 0;
}
static uint32_t sys_write(int32_t fd, char *str, uint32_t count) {
    if (fd < 0) {
        return (uint32_t)-1;
    }
    if (is_pipe(fd)) {
        return pipe_write(fd, str, count);
    }
    uint32_t gfd = fd_local2global((uint32_t)fd);
    struct file *wf = file_get(gfd);
    if (gfd >= 3 && wf != NULL && wf->fd_inode != NULL &&
        wf->fd_flag != PIPE_FLAG) {
        return write_file(fd, str, count);
    }
    TTY.write(str, count);
    return count;
}
static uint32_t sys_putchar(char c) {
    console_putc(c);
    return (uint32_t)(unsigned char)c;
}
static uint32_t sys_clear(void) {
    io_clear_screen();
    return 0;
}
static int32_t sys_read(int32_t fd, void *buf, uint32_t count) {
    if (fd == 1 || fd == 2)
        return -1;
    if (is_pipe(fd)) {
        return (int32_t)pipe_read(fd, buf, count);
    }
    if (fd == 0) {
        return TTY.read((char *)buf, count);
    }
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC)
        return -1;
    if (count == 0)
        return 0;
    int32_t r = (int32_t)read_file(fd, buf, count);
    return r;
}
static const char *task_status_str(enum task_status s) {
    switch (s) {
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_READY:
        return "READY";
    case TASK_BLOCKED:
        return "BLOCKED";
    case TASK_WAITING:
        return "WAITING";
    case TASK_HANGING:
        return "HANGING";
    case TASK_DIED:
        return "DIED";
    }
    return "?";
}
static int ps_action(struct task_struct *t, void *arg) {
    (void)arg;
    char buf[80];
    const char *parent = (t->parent_pid == -1) ? "(none)" : "?";
    if (t->parent_pid >= 0) {
        int n = 0;
        uint32_t v = (uint32_t)t->parent_pid;
        if (v == 0) {
            buf[n++] = '0';
        } else {
            char tmp[12];
            int m = 0;
            while (v) {
                tmp[m++] = (char)('0' + v % 10);
                v /= 10;
            }
            while (m--)
                buf[n++] = tmp[m];
        }
        buf[n] = 0;
        parent = buf;
    }
    kprintf("PID=%u PPID=%s STAT=%s TICKS=%u NAME=%s\n", t->pid, parent,
            task_status_str(t->status), t->elapsed_ticks, t->name);
    return 0;
}
static uint32_t sys_ps(void) {
    kprintf("=== ps ===\n");
    thread_traverse_all(ps_action, NULL);
    return 0;
}
uint32_t sys_brk(uint32_t addr) {
    struct task_struct *cur = current;
    if (cur->user_brk == 0) {
        cur->user_brk = USER_HEAP_BASE;
    }
    uint32_t base = USER_HEAP_BASE;
    uint32_t limit = USER_HEAP_LIMIT;
    uint32_t cur_brk = cur->user_brk;
    if (addr == 0) {
        return cur_brk;
    }
    uint32_t new_brk = addr;
    if (new_brk < base)
        new_brk = base;
    if (new_brk > limit)
        new_brk = limit;
    uint32_t old_page = (cur_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t new_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (new_page > old_page) {
        for (uint32_t p = old_page; p < new_page; p += PAGE_SIZE) {
            if (get_a_page(p) == 0) {
                kprintf("[brk] OOM, keep 0x%x\n", cur_brk);
                return cur_brk;
            }
        }
    } else if (new_page < old_page) {
        for (uint32_t p = new_page; p < old_page; p += PAGE_SIZE) {
            free_user_page(p);
        }
    }
    cur->user_brk = new_brk;
    return new_brk;
}
static uint32_t sys_set_thread_area(struct Registers *r, uint32_t base) {
    if (base == 0)
        return (uint32_t)-1;
    current->tls_base = base;
    current->tls_selector = SELECTOR_TLS;
    tls_desc_set_base(base);
    current->errno = 0;
    *(volatile int32_t *)base = 0;
    return 0;
}
uint32_t syscall_handler(struct Registers *r) {
    uint32_t nr = r->eax;
    uint32_t ret = (uint32_t)-1;
    int kcaller = (r->cs & 3) == 0;
    if (r->int_no == 0x81 || current->compat || nr >= COMPAT_SYSCALL_BASE) {
        check_pending_signals(r);
        if (r->int_no == 0x80 && nr < COMPAT_SYSCALL_BASE) {
            r->rdi = r->rbx;
            r->rsi = r->rcx;
            r->r10 = r->esi;
            r->r8 = r->edi;
            r->r9 = r->ebp;
        }
        return linux_compat_handler(r);
    }
    switch (nr) {
    case SYS_GETPID:
        ret = sys_getpid();
        break;
    case SYS_WRITE:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 0))
            break;
        ret = sys_write((int32_t)r->ebx, (char *)r->ecx, (uint32_t)r->edx);
        break;
    case SYS_PUTCHAR:
        ret = sys_putchar((char)r->ebx);
        break;
    case SYS_CLEAR:
        ret = sys_clear();
        break;
    case SYS_READ:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 1))
            break;
        ret = (uint32_t)sys_read((int32_t)r->ebx, (void *)r->ecx,
                                 (uint32_t)r->edx);
        break;
    case SYS_FORK:
        ret = (uint32_t)sys_fork(r);
        break;
    case SYS_GETCWD:
        if (!kcaller && !access_ok((const void *)r->ebx, (size_t)r->ecx, 1))
            break;
        ret = (uint32_t)sys_getcwd((char *)r->ebx, (uint32_t)r->ecx);
        break;
    case SYS_CHDIR:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_chdir((const char *)r->ebx);
        break;
    case SYS_MKDIR:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_mkdir((const char *)r->ebx);
        break;
    case SYS_RMDIR:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_rmdir((const char *)r->ebx);
        break;
    case SYS_OPEN:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)open_file((const char *)r->ebx, (uint8_t)r->ecx);
        break;
    case SYS_CLOSE:
        ret = (uint32_t)close_file((int)r->ebx);
        break;
    case SYS_LSEEK:
        ret = (uint32_t)sys_lseek((int32_t)r->ebx, (int32_t)r->ecx,
                                  (uint8_t)r->edx);
        break;
    case SYS_UNLINK:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_unlink((const char *)r->ebx);
        break;
    case SYS_OPENDIR:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_opendir((const char *)r->ebx);
        break;
    case SYS_CLOSEDIR:
        ret = (uint32_t)sys_closedir((struct dir *)r->ebx);
        break;
    case SYS_READDIR:
        ret = (uint32_t)sys_readdir((struct dir *)r->ebx);
        break;
    case SYS_REWINDDIR:
        sys_rewinddir((struct dir *)r->ebx);
        ret = 0;
        break;
    case SYS_STAT:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0) ||
            !kcaller &&
                !access_ok((const void *)r->ecx, sizeof(struct stat), 1))
            break;
        ret = (uint32_t)sys_stat((const char *)r->ebx, (struct stat *)r->ecx);
        break;
    case SYS_PS:
        sys_ps();
        ret = 0;
        break;
    case SYS_EXECV:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0) ||
            !kcaller && !access_ok((const void *)r->ecx, sizeof(void *), 0))
            break;
        ret =
            (uint32_t)sys_execv((const char *)r->ebx, (const char **)r->ecx, r);
        break;
    case SYS_EXIT:
        sys_exit((int32_t)r->ebx);
        ret = 0;
        break;
    case SYS_WAIT:
        if (!kcaller && r->ebx &&
            !access_ok((const void *)r->ebx, sizeof(int32_t), 1))
            break;
        ret = (uint32_t)sys_wait((int32_t *)r->ebx);
        break;
    case SYS_PIPE:
        if (!kcaller &&
            !access_ok((const void *)r->ebx, 2 * sizeof(int32_t), 1))
            break;
        ret = (uint32_t)sys_pipe((int32_t *)r->ebx);
        break;
    case SYS_FD_REDIRECT:
        sys_fd_redirect((uint32_t)r->ebx, (uint32_t)r->ecx);
        ret = 0;
        break;
    case SYS_GUI:
        ret = (uint32_t)gui_session_run();
        break;
    case SYS_BRK:
        ret = (uint32_t)sys_brk((uint32_t)r->ebx);
        break;
    case SYS_SIGACTION:
        if ((r->ecx && !kcaller &&
             !access_ok((const void *)r->ecx, sizeof(struct sigaction), 0)) ||
            (r->edx && !kcaller &&
             !access_ok((const void *)r->edx, sizeof(struct sigaction), 1)))
            break;
        ret = (uint32_t)sys_sigaction((int)r->ebx,
                                      (const struct sigaction *)r->ecx,
                                      (struct sigaction *)r->edx);
        break;
    case SYS_KILL:
        ret = (uint32_t)sys_kill((int)r->ebx, (int)r->ecx);
        break;
    case SYS_SIGRETURN:
        sys_sigreturn(r);
        ret = 0;
        break;
    case SYS_SIGPROCMASK:
        if ((r->ecx && !kcaller &&
             !access_ok((const void *)r->ecx, sizeof(sigset_t), 0)) ||
            (r->edx && !kcaller &&
             !access_ok((const void *)r->edx, sizeof(sigset_t), 1)))
            break;
        ret = (uint32_t)sys_sigprocmask((int)r->ebx, (const sigset_t *)r->ecx,
                                        (sigset_t *)r->edx);
        break;
    case SYS_SET_THREAD_AREA:
        if (!kcaller && !access_ok((const void *)r->ebx, sizeof(int32_t), 1))
            break;
        ret = sys_set_thread_area(r, (uint32_t)r->ebx);
        break;
    case SYS_MMAP:
        if (!kcaller &&
            !access_ok((const void *)r->ebx, sizeof(struct mmap_args), 0))
            break;
        ret = sys_mmap((const struct mmap_args *)r->ebx);
        break;
    case SYS_MUNMAP:
        ret = (uint32_t)sys_munmap((uint32_t)r->ebx, (uint32_t)r->ecx);
        break;
    case SYS_MMAP2:
        ret = sys_mmap2((uint32_t)r->ebx, (uint32_t)r->ecx, (uint32_t)r->edx,
                        (uint32_t)r->esi, (uint32_t)r->edi, (uint32_t)r->r10);
        break;
    case SYS_MPROTECT:
        ret = (uint32_t)sys_mprotect((uint32_t)r->ebx, (uint32_t)r->ecx,
                                     (uint32_t)r->edx);
        break;
    case SYS_FUTEX:
        ret = (uint32_t)sys_futex((uint32_t)r->ebx, (uint32_t)r->ecx,
                                  (uint32_t)r->edx, (uint32_t)r->esi);
        break;
    case SYS_CLONE:
        ret = (uint32_t)sys_clone(r);
        break;
    case SYS_FSTAT:
        if (!kcaller &&
            !access_ok((const void *)r->ecx, sizeof(struct stat), 1))
            break;
        ret = (uint32_t)sys_fstat((int32_t)r->ebx, (void *)r->ecx);
        break;
    case SYS_DUP:
        ret = (uint32_t)sys_dup((int32_t)r->ebx);
        break;
    case SYS_DUP2:
        ret = (uint32_t)sys_dup2((int32_t)r->ebx, (int32_t)r->ecx);
        break;
    case SYS_FCNTL:
        ret = (uint32_t)sys_fcntl((int32_t)r->ebx, (int32_t)r->ecx,
                                  (uint32_t)r->edx);
        break;
    case SYS_GETDENTS:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 1))
            break;
        ret = (uint32_t)sys_getdents((int32_t)r->ebx, (void *)r->ecx,
                                     (uint32_t)r->edx);
        break;
    case SYS_READLINK:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0) ||
            !kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 1))
            break;
        ret = (uint32_t)sys_readlink((const char *)r->ebx, (char *)r->ecx,
                                     (uint32_t)r->edx);
        break;
    case SYS_ACCESS:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_access((const char *)r->ebx, (int32_t)r->ecx);
        break;
    case SYS_RENAME:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0) ||
            !kcaller && !access_ok((const void *)r->ecx, 1, 0))
            break;
        ret = (uint32_t)sys_rename((const char *)r->ebx, (const char *)r->ecx);
        break;
    case SYS_TRUNCATE:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_truncate((const char *)r->ebx, (int32_t)r->ecx);
        break;
    case SYS_CHMOD:
        if (!kcaller && !access_ok((const void *)r->ebx, 1, 0))
            break;
        ret = (uint32_t)sys_chmod((const char *)r->ebx, (uint32_t)r->ecx);
        break;
    case SYS_CLOCK_GETTIME:
        if (!kcaller &&
            !access_ok((const void *)r->ecx, sizeof(struct timespec), 1))
            break;
        ret = (uint32_t)sys_clock_gettime((int32_t)r->ebx,
                                          (struct timespec *)r->ecx);
        break;
    case SYS_GETTIMEOFDAY:
        if (!kcaller &&
            !access_ok((const void *)r->ebx, sizeof(struct timeval), 1))
            break;
        ret = (uint32_t)sys_gettimeofday((struct timeval *)r->ebx,
                                         (void *)r->ecx);
        break;
    case SYS_NANOSLEEP:
        if (!kcaller &&
            !access_ok((const void *)r->ebx, sizeof(struct timespec), 0))
            break;
        ret = (uint32_t)sys_nanosleep((const struct timespec *)r->ebx,
                                      (struct timespec *)r->ecx);
        break;
    case SYS_GETUID:
        ret = sys_getuid();
        break;
    case SYS_GETGID:
        ret = sys_getgid();
        break;
    case SYS_GETEUID:
        ret = sys_geteuid();
        break;
    case SYS_GETEGID:
        ret = sys_getegid();
        break;
    case SYS_EXIT_GROUP:
        sys_exit_group((int32_t)r->ebx);
        ret = 0;
        break;
    case SYS_ICMP_SEND:
        ret = (uint32_t)nt_icmp_send((uint32_t)r->ebx, (uint16_t)r->ecx,
                                     (uint16_t)r->edx);
        break;
    case SYS_ICMP_RECV:
        if (!kcaller &&
            !access_ok((const void *)r->ebx, sizeof(struct nt_ping_reply), 1))
            break;
        ret =
            (uint32_t)nt_icmp_recv((struct nt_ping_reply *)r->ebx, (int)r->ecx);
        break;
    case SYS_SHUTDOWN:
        ret = sys_shutdown();
        break;
    case SYS_SOCKET:
        ret = (uint32_t)net_socket((int)r->ebx, (int)r->ecx, (int)r->edx);
        break;
    case SYS_BIND:
        ret =
            (uint32_t)net_bind((int)r->ebx, (uint32_t)r->ecx, (uint16_t)r->edx);
        break;
    case SYS_LISTEN:
        ret = (uint32_t)net_listen((int)r->ebx, (int)r->ecx);
        break;
    case SYS_CONNECT:
        ret = (uint32_t)net_connect((int)r->ebx, (uint32_t)r->ecx,
                                    (uint16_t)r->edx);
        break;
    case SYS_SEND:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 0))
            break;
        ret = (uint32_t)net_send((int)r->ebx, (const void *)r->ecx,
                                 (uint32_t)r->edx);
        break;
    case SYS_RECV:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 1))
            break;
        ret = (uint32_t)net_recv((int)r->ebx, (void *)r->ecx, (uint32_t)r->edx);
        break;
    case SYS_SENDTO:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 0))
            break;
        ret = (uint32_t)net_sendto((int)r->ebx, (const void *)r->ecx,
                                   (uint32_t)r->edx, (uint32_t)r->esi,
                                   (uint16_t)r->edi);
        break;
    case SYS_RECVFROM:
        if (!kcaller && !access_ok((const void *)r->ecx, (size_t)r->edx, 1))
            break;
        ret = (uint32_t)net_recvfrom((int)r->ebx, (void *)r->ecx,
                                     (uint32_t)r->edx, (uint32_t *)r->esi,
                                     (uint16_t *)r->edi);
        break;
    case SYS_ACCEPT:
        ret = (uint32_t)net_accept((int)r->ebx);
        break;
    case SYS_CLOSE_SOCKET:
        ret = (uint32_t)net_close((int)r->ebx);
        break;
    case SYS_SOCK_SHUTDOWN:
        ret = (uint32_t)net_shutdown((int)r->ebx, (int)r->ecx);
        break;
    case SYS_GETSOCKNAME:
        if (!kcaller && !access_ok((void *)r->ecx, 4, 1) ||
            !kcaller && !access_ok((void *)r->edx, 2, 1))
            break;
        ret = (uint32_t)net_getsockname((int)r->ebx, (uint32_t *)r->ecx,
                                        (uint16_t *)r->edx);
        break;
    case SYS_GETPEERNAME:
        if (!kcaller && !access_ok((void *)r->ecx, 4, 1) ||
            !kcaller && !access_ok((void *)r->edx, 2, 1))
            break;
        ret = (uint32_t)net_getpeername((int)r->ebx, (uint32_t *)r->ecx,
                                        (uint16_t *)r->edx);
        break;
    case SYS_GETSOCKOPT:
        if (!kcaller && !access_ok((void *)r->esi, 4, 1) ||
            !kcaller && !access_ok((void *)r->edi, 4, 1))
            break;
        ret = (uint32_t)net_getsockopt((int)r->ebx, (int)r->ecx, (int)r->edx,
                                       (void *)r->esi, (uint32_t *)r->edi);
        break;
    case SYS_SETSOCKOPT:
        if (!kcaller && !access_ok((void *)r->esi, 4, 0))
            break;
        ret = (uint32_t)net_setsockopt((int)r->ebx, (int)r->ecx, (int)r->edx,
                                       (const void *)r->esi, (uint32_t)r->edi);
        break;
    case SYS_SOCK_FCNTL:
        ret = (uint32_t)net_fcntl((int)r->ebx, (int)r->ecx, (uint32_t)r->edx);
        break;
    case SYS_SELECT:
        if (r->ecx && !kcaller && !access_ok((void *)r->ecx, 8, 1))
            break;
        if (r->edx && !kcaller && !access_ok((void *)r->edx, 8, 1))
            break;
        if (r->esi && !kcaller && !access_ok((void *)r->esi, 8, 1))
            break;
        ret = (uint32_t)net_select((int)r->ebx, (uint32_t *)r->ecx,
                                   (uint32_t *)r->edx, (uint32_t *)r->esi,
                                   (int)r->edi);
        break;
    default:
        ret = (uint32_t)-1;
        break;
    }
    check_pending_signals(r);
    return ret;
}
void syscall_init(void) {
    extern void syscall_entry(void);
    extern uint64_t syscall_kstack_top_data;
    const uint64_t MSR_STAR = 0xC0000081;
    const uint64_t MSR_LSTAR = 0xC0000082;
    const uint64_t MSR_FMASK = 0xC0000084;
    const uint64_t MSR_EFER = 0xC0000080;

    uint64_t star = ((uint64_t)0x33 << 48) | ((uint64_t)0x08 << 32);
    asm_wrmsr(MSR_STAR, star);
    asm_wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    asm_wrmsr(MSR_FMASK, 0x5700);
    uint64_t efer = asm_rdmsr(MSR_EFER);
    asm_wrmsr(MSR_EFER, efer | 1);
    syscall_kstack_top_data = 0;

    kprintf("[OK] syscall init, 0x80 full table + syscall/sysret entry\n");
}
