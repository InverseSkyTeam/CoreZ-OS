// 参考: 《操作系统真相还原》(于渊) 第15章 fork + Linux
#include "./clone.h"

#include "../fs/file.h"
#include "../include/asmFunc.h"
#include "../include/assert.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "../shell/pipe.h"
#include "../thread/thread.h"
#include "../userprog/process.h"

extern void intr_exit(void);

static void build_clone_stack(struct task_struct *child,
                              struct Registers *parent_frame,
                              uint32_t user_stack) {
    uint32_t stack_top = child->kernel_stack_top;
    struct Registers *child_frame =
        (struct Registers *)(stack_top - sizeof(struct Registers));
    memcpy(child_frame, parent_frame, sizeof(struct Registers));
    child_frame->eax = 0;
    child_frame->user_esp = user_stack;
    struct thread_stack *ts =
        (struct thread_stack *)((uint32_t)child_frame - 24);
    memset(ts, 0, 24);
    ts->eflags = 0x202;
    ts->eip = (void (*)(void))intr_exit;
    child->self_kstack = (uint32_t *)ts;
}

pid_t sys_clone(struct Registers *r) {
    uint32_t flags = r->ebx;
    uint32_t child_user_stack = r->ecx;
    struct task_struct *parent = current_task;
    struct task_struct *child =
        thread_alloc_slot(parent->name, parent->priority);
    if (child == NULL) {
        return -1;
    }
    child->parent_pid = (int32_t)parent->pid;
    child->cwd_inode_nr = parent->cwd_inode_nr;
    child->user_brk = parent->user_brk;
    for (uint32_t i = 0; i < MAX_FILES_OPEN_PER_PROC; i++) {
        child->fd_table[i] = parent->fd_table[i];
        if (child->fd_table[i] != (uint32_t)-1 &&
            child->fd_table[i] < MAX_FILE_OPEN &&
            file_table[child->fd_table[i]].fd_flag == PIPE_FLAG) {
            file_table[child->fd_table[i]].fd_pos++;
        }
    }
    child->exit_status = 0;

    child->signal_mask = parent->signal_mask;
    child->signal_pending = 0;
    for (int i = 0; i < NSIG; i++) {
        child->sigactions[i] = parent->sigactions[i];
    }

    child->pgdir = parent->pgdir;
    child->tls_base = parent->tls_base;
    child->tls_selector = parent->tls_selector;

    if (child_user_stack == 0) {
        child_user_stack = (uint32_t)get_a_page(USER_STACK3_VADDR) + PAGE_SIZE;
    }

    build_clone_stack(child, r, child_user_stack);

    child->status = TASK_BLOCKED;
    thread_ready(child);
    (void)flags;
    return (pid_t)child->pid;
}