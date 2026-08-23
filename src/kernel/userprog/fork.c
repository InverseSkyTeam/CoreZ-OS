// 参考: 《操作系统真相还原》(于渊) 第15章 fork
#include "./fork.h"

#include "../fs/file.h"
#include "../include/asmFunc.h"
#include "../include/assert.h"
#include "../lib/str/str.h"
#include "../memory/bitmap/bitmap.h"
#include "../memory/pool/pool.h"
#include "../shell/pipe.h"
#include "../thread/thread.h"
#include "../userprog/process.h"

extern void intr_exit(void);

static void mark_child_bitmap(struct task_struct *child, uint32_t vaddr) {
    uint32_t bit = (vaddr - USER_VADDR_START) / PAGE_SIZE;
    if (vaddr >= USER_VADDR_START &&
        bit < child->userprog_v_addr.vaddr_bitmap.btmp_bytes_len * 8) {
        bitmap_set(&child->userprog_v_addr.vaddr_bitmap, bit, 1);
    }
}

static void copy_user_space(struct task_struct *parent,
                            struct task_struct *child) {
    uint32_t *pgdir = (uint32_t *)parent->pgdir;
    uint32_t *child_pgdir = (uint32_t *)child->pgdir;

    for (uint32_t pde_idx = 0; pde_idx < 768; pde_idx++) {
        uint32_t pde = pgdir[pde_idx];
        if (!(pde & 1) || (pde & 0x80)) {
            continue;
        }

        uint32_t child_tbl = (uint32_t)palloc(&kernel_pool);
        if (child_tbl == 0) {
            return;
        }
        memset((void *)child_tbl, 0, PAGE_SIZE);

        uint32_t *first_pte = pte_ptr(pde_idx * 0x400000);
        for (uint32_t pte_idx = 0; pte_idx < 1024; pte_idx++) {
            uint32_t pte = first_pte[pte_idx];
            if (!(pte & 1)) {
                continue;
            }
            uint32_t phy = pte & 0xfffff000;
            uint32_t vaddr = pde_idx * 0x400000 + pte_idx * 0x1000;
            if (vaddr == phy) { 
                continue;
            }
            if (pte & COW_FLAG) {
                page_incr_shared(phy);
                mark_child_bitmap(child, vaddr);
                ((uint32_t *)child_tbl)[pte_idx] = pte;  
            } else if (pte & 2) { 
                first_pte[pte_idx] = (pte & ~(uint32_t)2) | COW_FLAG; 

                __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
                mark_child_bitmap(child, vaddr);
                page_incr_shared(phy);
                ((uint32_t *)child_tbl)[pte_idx] =
                    (pte & ~(uint32_t)2) | COW_FLAG;  
            } else {
                page_incr_shared(phy);
                mark_child_bitmap(child, vaddr);
                ((uint32_t *)child_tbl)[pte_idx] = pte;
            }
        }
        child_pgdir[pde_idx] = child_tbl | (pde & 0xfff);
    }

    memcpy(child_pgdir + 768, (void *)((uint32_t)pgdir + 768 * 4),
           255 * sizeof(uint32_t));
    child_pgdir[1023] = (uint32_t)child->pgdir | 7;
}

static void build_child_stack(struct task_struct *child,
                              struct Registers *parent_frame) {
    uint32_t stack_top = child->kernel_stack_top;
    struct Registers *child_frame =
        (struct Registers *)(stack_top - sizeof(struct Registers));
    memcpy(child_frame, parent_frame, sizeof(struct Registers));
    child_frame->eax = 0;
    struct thread_stack *ts =
        (struct thread_stack *)((uint32_t)child_frame - 24);
    memset(ts, 0, 24);
    ts->eflags = 0x202;
    ts->eip = (void (*)(void))intr_exit;
    child->self_kstack = (uint32_t *)ts;
}

pid_t sys_fork(struct Registers *r) {
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

    create_user_vaddr_bitmap(child);
    child->pgdir = (uint32_t)create_page_dir();
    if (child->pgdir == 0) {
        return -1;
    }
    copy_user_space(parent, child);
    build_child_stack(child, r);

    child->status = TASK_BLOCKED;
    thread_ready(child);
    return (pid_t)child->pid;
}
