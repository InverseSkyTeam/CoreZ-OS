// 参考: Wine syscall 翻译(gitlab.winehq.org/wine), Linux set_thread_area
#include "./linux_compat.h"

#include "../device/ioqueue.h"
#include "../device/keyboard.h"
#include "../fs/fs.h"
#include "../include/asmFunc.h"
#include "../initer/gdt/gdt.h"
#include "../initer/io/io.h"
#include "../shell/pipe.h"
#include "../thread/thread.h"
#include "../userprog/process.h"
#include "../userprog/wait_exit.h"

uint32_t sys_brk(uint32_t addr);

struct lc_iovec {
    uint32_t base;
    uint32_t len;
};

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
    r->gs = SELECTOR_TLS;
    lc_seterrno(cur, 0);
    return 0;
}

static int32_t sys_compat_writev(int32_t fd, struct lc_iovec *iov,
                                 int32_t iovcnt) {
    if (iovcnt < 0)
        return -1;
    uint32_t total = 0;
    for (int32_t i = 0; i < iovcnt; i++) {
        if (iov[i].len == 0)
            continue;
        int32_t n = compat_write(fd, (const void *)iov[i].base, iov[i].len);
        if (n < 0)
            return -1;
        total += (uint32_t)n;
    }
    return (int32_t)total;
}

uint32_t linux_compat_handler(struct Registers *r) {
    struct task_struct *cur = current_task;
    uint32_t nr = r->eax;
    uint32_t ret = (uint32_t)-1;

    cur->compat = 1;

    uint32_t a = r->ebx;
    uint32_t b = r->ecx;
    uint32_t c = r->edx;
    uint32_t d = r->esi;
    uint32_t e = r->edi;
    uint32_t f = r->ebp;

    switch (nr) {
    case LC_PID:
        ret = cur->pid;
        break;
    case LC_WRITE: {
        int32_t n = compat_write((int32_t)a, (const void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case LC_READ: {
        int32_t n = compat_read((int32_t)a, (void *)b, c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    case LC_EXIT:
        sys_exit((int32_t)a);
        ret = 0;
        break;
    case LC_BRK: {
        uint32_t rr = sys_brk(a);
        lc_seterrno(cur, 0);
        ret = rr;
        break;
    }
    case LC_OPEN: {
        int32_t fd = open_file((const char *)a, (uint8_t)b);
        lc_seterrno(cur, fd < 0 ? -fd : 0);
        ret = fd < 0 ? (uint32_t)-1 : (uint32_t)fd;
        break;
    }
    case LC_CLOSE: {
        int32_t rr = close_file((int32_t)a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : (uint32_t)rr;
        break;
    }
    case LC_SET_THREAD_AREA: {
        int32_t rr = compat_set_thread_area(r, cur, a);
        lc_seterrno(cur, rr < 0 ? -rr : 0);
        ret = rr < 0 ? (uint32_t)-1 : 0;
        break;
    }
    case LC_MMAP: {
        kprintf(
            "[compat] mmap(0x%x,%u,prot=0x%x,flags=0x%x,fd=%u,off=%u)  <- 6 "
            "arg\n",
            a, b, c, d, e, f);
        if (a != 0) {
            ret = a;
        } else {
            ret = compat_brk_alloc(cur, b);
        }
        lc_seterrno(cur, (int32_t)ret < 0 ? 1 : 0);
        break;
    }
    case LC_WRITEV: {
        int32_t n =
            sys_compat_writev((int32_t)a, (struct lc_iovec *)b, (int32_t)c);
        lc_seterrno(cur, n < 0 ? -n : 0);
        ret = n < 0 ? (uint32_t)-1 : (uint32_t)n;
        break;
    }
    default:
        ret = (uint32_t)-1;
        break;
    }

    return ret;
}