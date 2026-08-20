// 参考: 《操作系统真相还原》(于渊) 第12章 系统调用

#include "./syscall.h"
#include "../thread/thread.h"
#include "../initer/io/io.h"
#include "../lib/str/str.h"
#include "../include/assert.h"
#include "../include/asmFunc.h"
#include "../device/keyboard.h"
#include "../device/ioqueue.h"
#include "../fs/fs.h"
#include "../fs/file.h"
#include "../userprog/process.h"
#include "../userprog/exec.h"
#include "../userprog/fork.h"
#include "../userprog/wait_exit.h"
#include "../shell/pipe.h"
#include "../gui/gui.h"

static uint32_t sys_getpid(void) {
    return current_task->pid;
}

static uint32_t sys_write(int32_t fd, char* str, uint32_t count) {
    if (fd < 0) {
        return (uint32_t)-1;
    }

    if (is_pipe(fd)) {
        return pipe_write(fd, str, count);
    }
    for (uint32_t i = 0; i < count; i++) {
        console_putc(str[i]);
    }
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

static int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    if (fd == 1 || fd == 2) return -1;

    if (is_pipe(fd)) {
        return (int32_t)pipe_read(fd, buf, count);
    }
    if (fd == 0) {
        uint8_t* p = (uint8_t*)buf;
        uint32_t got = 0;
        asm_cli();
        while (got < count) {
            char c = ioq_getchar(&keyboard_ioq);
            asm_sti();
            p[got++] = (uint8_t)c;
            if (c == '\n' || c == '\r') break;
            asm_cli();
        }
        asm_sti();
        return (int32_t)got;
    }
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) return -1;
    if (count == 0) return 0;
    int32_t r = (int32_t)read_file(fd, buf, count);
    return r;
}

static const char* task_status_str(enum task_status s) {
    switch (s) {
    case TASK_RUNNING:  return "RUNNING";
    case TASK_READY:    return "READY";
    case TASK_BLOCKED:  return "BLOCKED";
    case TASK_WAITING:  return "WAITING";
    case TASK_HANGING:  return "HANGING";
    case TASK_DIED:     return "DIED";
    }
    return "?";
}

static int ps_action(struct task_struct* t, void* arg) {
    (void)arg;
    char buf[80];
    const char* parent = (t->parent_pid == -1) ? "(none)" : "?";
    if (t->parent_pid >= 0) {
        int n = 0;
        uint32_t v = (uint32_t)t->parent_pid;
        if (v == 0) { buf[n++] = '0'; }
        else {
            char tmp[12]; int m = 0;
            while (v) { tmp[m++] = (char)('0' + v % 10); v /= 10; }
            while (m--) buf[n++] = tmp[m];
        }
        buf[n] = 0;
        parent = buf;
    }
    kprintf("PID=%u PPID=%s STAT=%s TICKS=%u NAME=%s\n",
            t->pid, parent, task_status_str(t->status),
            t->elapsed_ticks, t->name);
    return 0;
}

static uint32_t sys_ps(void) {
    kprintf("=== ps ===\n");
    thread_traverse_all(ps_action, NULL);
    return 0;
}

static uint32_t sys_brk(uint32_t addr) {
    struct task_struct* cur = current_task;
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
    if (new_brk < base)  new_brk = base;
    if (new_brk > limit) new_brk = limit;

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

uint32_t syscall_handler(struct Registers* r) {
    uint32_t nr = r->eax;
    uint32_t ret = (uint32_t)-1;
    switch (nr) {
    case SYS_GETPID:
        ret = sys_getpid();
        break;
    case SYS_WRITE:
        ret = sys_write((int32_t)r->ebx, (char*)r->ecx, (uint32_t)r->edx);
        break;
    case SYS_PUTCHAR:
        ret = sys_putchar((char)r->ebx);
        break;
    case SYS_CLEAR:
        ret = sys_clear();
        break;
    case SYS_READ:
        ret = (uint32_t)sys_read((int32_t)r->ebx, (void*)r->ecx, (uint32_t)r->edx);
        break;
    case SYS_FORK:
        ret = (uint32_t)sys_fork(r);
        break;
    case SYS_GETCWD:
        ret = (uint32_t)sys_getcwd((char*)r->ebx, (uint32_t)r->ecx);
        break;
    case SYS_CHDIR:
        ret = (uint32_t)sys_chdir((const char*)r->ebx);
        break;
    case SYS_MKDIR:
        ret = (uint32_t)sys_mkdir((const char*)r->ebx);
        break;
    case SYS_RMDIR:
        ret = (uint32_t)sys_rmdir((const char*)r->ebx);
        break;
    case SYS_OPEN:
        ret = (uint32_t)open_file((const char*)r->ebx, (uint8_t)r->ecx);
        break;
    case SYS_CLOSE:
        ret = (uint32_t)close_file((int)r->ebx);
        break;
    case SYS_LSEEK:
        ret = (uint32_t)sys_lseek((int32_t)r->ebx, (int32_t)r->ecx, (uint8_t)r->edx);
        break;
    case SYS_UNLINK:
        ret = (uint32_t)sys_unlink((const char*)r->ebx);
        break;
    case SYS_OPENDIR:
        ret = (uint32_t)sys_opendir((const char*)r->ebx);
        break;
    case SYS_CLOSEDIR:
        ret = (uint32_t)sys_closedir((struct dir*)r->ebx);
        break;
    case SYS_READDIR:
        ret = (uint32_t)sys_readdir((struct dir*)r->ebx);
        break;
    case SYS_REWINDDIR:
        sys_rewinddir((struct dir*)r->ebx);
        ret = 0;
        break;
    case SYS_STAT:
        ret = (uint32_t)sys_stat((const char*)r->ebx, (struct stat*)r->ecx);
        break;
    case SYS_PS:
        sys_ps();
        ret = 0;
        break;
    case SYS_EXECV:
        ret = (uint32_t)sys_execv((const char*)r->ebx, (const char**)r->ecx);
        break;
    case SYS_EXIT:
        sys_exit((int32_t)r->ebx);   
        ret = 0;
        break;
    case SYS_WAIT:
        ret = (uint32_t)sys_wait((int32_t*)r->ebx);
        break;
    case SYS_PIPE:
        ret = (uint32_t)sys_pipe((int32_t*)r->ebx);
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
        ret = (uint32_t)sys_sigaction((int)r->ebx,
                                      (const struct sigaction*)r->ecx,
                                      (struct sigaction*)r->edx);
        break;
    case SYS_KILL:
        ret = (uint32_t)sys_kill((int)r->ebx, (int)r->ecx);
        break;
    case SYS_SIGRETURN:
        sys_sigreturn(r);
        ret = 0;
        break;
    case SYS_SIGPROCMASK:
        ret = (uint32_t)sys_sigprocmask((int)r->ebx,
                                        (const sigset_t*)r->ecx,
                                        (sigset_t*)r->edx);
        break;
    default:
        ret = (uint32_t)-1;
        break;
    }

    
    check_pending_signals(r);
    return ret;
}

void syscall_init(void) {
    kprintf("[OK] syscall init, 0x80 registered (full table)\n");
}
