#include "kernel/sched/thread.h"
#include "drivers/char/keyboard.h"
#include "kernel/asmFunc.h"
#include "kernel/assert.h"
#include "drivers/char/console/io.h"
#include "lib/list/list.h"
#include "lib/rbtree/rbtree.h"
#include "lib/str/str.h"
#include "kernel/mm/pool/pool.h"
#include "kernel/userprog/process.h"
#include "kernel/userprog/wait_exit.h"

struct RB_ROOT ready_rb_root;
static uint64_t global_seq = 0;

static struct task_struct task_table[MAX_TASKS];
static uint32_t pid_alloc = 0;
static uint32_t died_pending = 0;
struct list thread_all_list;
struct task_struct *idle_thread;
uint32_t foreground_pid = (uint32_t)-1;
static volatile uint32_t idle_monitor;
static int mwait_ok;

void cpu_idle_init(void) {
    mwait_ok = asm_mwait_supported();
    if (mwait_ok)
        kprintf("[idle] Enable MONITOR/MWAIT\n");
    else
        kprintf("[idle] Enable HLT\n");
}

void cpu_idle(void) {
    if (mwait_ok)
        asm_sti_mwait((uint64_t)(uintptr_t)&idle_monitor);
    else
        asm_stihlt();
}

static void idle(void *arg) {
    for (;;) {
        thread_block();
        cpu_idle();
    }
}

void kernel_thread_entry_c(thread_func function, void *arg) {
    function(arg);
    thread_exit_current();
}

static void init_fd_table(struct task_struct *t) {
    t->fd_table[0] = 0;
    t->fd_table[1] = 1;
    t->fd_table[2] = 2;
    for (uint32_t fd_idx = 3; fd_idx < MAX_FILES_OPEN_PER_PROC; fd_idx++)
        t->fd_table[fd_idx] = (uint32_t)-1;
    t->cwd_inode_nr = 0;
}

static void init_task_struct_basic(struct task_struct *t, int32_t parent_pid) {
    t->status = TASK_READY;
    t->pid = pid_alloc++;
    t->elapsed_ticks = 0;
    t->kernel_stack_top = 0;
    t->pgdir = 0;
    init_fd_table(t);
    t->parent_pid = parent_pid;
    t->stack_magic = STACK_MAGIC;
    t->tls_base = 0;
    t->tls_selector = 0;
    t->errno = 0;
    t->compat = 0;
    init_signal_state(t);

    t->rb_node.parent = NULL;
    t->rb_node.left = NULL;
    t->rb_node.right = NULL;
    t->rb_node.color = RB_RED;
    t->rb_node.key = 0;
    t->in_ready = 0;

    t->all_list_tag.prev = t->all_list_tag.next = NULL;
    t->futex_tag.prev = t->futex_tag.next = NULL;
    t->futex_ready = 0;
}

static void ready_enqueue(struct task_struct *t) {
    if (t->in_ready)
        return;
    t->rb_node.key = ++global_seq;
    rb_insert(&ready_rb_root, &t->rb_node);
    t->in_ready = 1;
    t->status = TASK_READY;
}

static void reap_died_threads(void);

static void ready_remove(struct task_struct *t) {
    if (!t->in_ready)
        return;
    rb_erase(&ready_rb_root, &t->rb_node);
    t->in_ready = 0;
}

struct task_struct *thread_create(char *name, uint8_t priority,
                                  thread_func function, void *arg) {
    struct task_struct *t = thread_alloc_slot(name, priority);
    if (t == NULL) {
        return NULL;
    }
    struct thread_stack *ts =
        (struct thread_stack *)(t->kernel_stack_top -
                                sizeof(struct thread_stack));
    ts->rflags = RFLAGS_INIT;
    ts->r15 = (uint64_t)function;
    ts->r14 = (uint64_t)arg;
    ts->r13 = ts->r12 = ts->rbx = ts->rbp = 0;
    ts->rip = kernel_thread_entry;
    ready_enqueue(t);
    return t;
}

void thread_init(void) {
    cpu_idle_init();
    rb_root_init(&ready_rb_root);
    list_init(&thread_all_list);

    set_current(&task_table[0]);
    task_table[0].self_kstack = 0;
    task_table[0].status = TASK_RUNNING;
    task_table[0].pid = pid_alloc++;
    strcpy(task_table[0].name, "main");
    task_table[0].priority = 5;
    task_table[0].ticks = 5;
    task_table[0].elapsed_ticks = 0;
    task_table[0].kernel_stack_top = 0;
    task_table[0].pgdir = 0;
    init_fd_table(&task_table[0]);
    task_table[0].parent_pid = -1;
    task_table[0].stack_magic = STACK_MAGIC;
    task_table[0].tls_base = 0;
    task_table[0].tls_selector = 0;
    task_table[0].errno = 0;
    task_table[0].compat = 0;
    task_table[0].in_ready = 0;
    task_table[0].rb_node.parent = task_table[0].rb_node.left =
        task_table[0].rb_node.right = NULL;
    task_table[0].rb_node.color = RB_BLACK;
    task_table[0].futex_tag.prev = task_table[0].futex_tag.next = NULL;
    task_table[0].futex_ready = 0;
    task_table[0].slot_used = 1;
    list_append(&thread_all_list, &task_table[0].all_list_tag);

    idle_thread = thread_create("idle", 10, idle, 0);
}

struct task_struct *thread_alloc_slot(const char *name, uint8_t priority) {
    struct task_struct *t = NULL;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (!task_table[i].slot_used) {
            t = &task_table[i];
            break;
        }
    }
    if (t == NULL) {
        kprintf("[thread] no free task slot (MAX_TASKS=%d)\n", MAX_TASKS);
        return NULL;
    }
    uint64_t stack = (uint64_t)get_kernel_pages(THREAD_STACK_SIZE / PAGE_SIZE);
    if (stack == 0) {
        kprintf("[thread] no kernel pages for stack\n");
        return NULL;
    }
    t->slot_used = 1;
    struct thread_stack *ts =
        (struct thread_stack *)(stack + THREAD_STACK_SIZE -
                                sizeof(struct thread_stack));
    ts->rflags = RFLAGS_INIT;
    ts->r15 = ts->r14 = ts->r13 = ts->r12 = ts->rbx = ts->rbp = 0;
    ts->rip = 0;
    t->self_kstack = (uint64_t *)ts;
    init_task_struct_basic(t, -1);
    strcpy(t->name, name);
    t->priority = priority;
    t->ticks = priority;
    t->kernel_stack_top = stack + THREAD_STACK_SIZE;
    list_append(&thread_all_list, &t->all_list_tag);

    return t;
}

void thread_ready(struct task_struct *t) {
    if (t == NULL)
        return;
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (!t->in_ready) {
        t->status = TASK_READY;
        ready_enqueue(t);
    }
    asm_restore_eflags(old);
}

void kernel_thread(char *name, uint8_t priority, thread_func function,
                   void *arg) {
    thread_create(name, priority, function, arg);
}

void thread_block(void) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (current->in_ready)
        ready_remove(current);
    current->status = TASK_BLOCKED;
    schedule();
    asm_restore_eflags(old);
}

void thread_block_with_status(enum task_status status) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (current->in_ready)
        ready_remove(current);
    current->status = status;
    schedule();
    asm_restore_eflags(old);
}

void thread_unblock(struct task_struct *t) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    ASSERT(t->status == TASK_BLOCKED || t->status == TASK_WAITING ||
           t->status == TASK_HANGING);
    if (!t->in_ready) {
        t->status = TASK_READY;
        ready_enqueue(t);
    }
    asm_restore_eflags(old);
}

void thread_yield(void) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (!current->in_ready) {
        current->status = TASK_READY;
        ready_enqueue(current);
    }
    current->ticks = current->priority;
    schedule();
    asm_restore_eflags(old);
}

void schedule(void) {
    ASSERT((asm_save_eflags() & 0x200) == 0);

    if (current->status == TASK_RUNNING) {
        if (current->in_ready)
            ready_remove(current);
        current->status = TASK_READY;
        ready_enqueue(current);
        current->ticks = current->priority;
    }

    if (died_pending > 0)
        reap_died_threads();

    if (rb_empty(&ready_rb_root)) {
        thread_unblock(idle_thread);
    }

    struct RB_NODE *node = rb_first(&ready_rb_root);
    struct task_struct *next = rb_entry(node, struct task_struct, rb_node);
    rb_erase(&ready_rb_root, node);
    next->in_ready = 0;
    next->status = TASK_RUNNING;

    struct task_struct *prev = current;
    set_current(next);
    process_activate(next);
    switch_to(&prev->self_kstack, &next->self_kstack);
}

int thread_traverse_all(thread_all_action action, void *arg) {
    int stopped = 0;
    struct list_elem *e = thread_all_list.head.next;
    while (e != &thread_all_list.tail) {
        struct task_struct *t = list_entry(e, struct task_struct, all_list_tag);
        struct list_elem *next = e->next;
        int r = action(t, arg);
        if (r) {
            stopped = 1;
            break;
        }
        e = next;
    }
    return stopped;
}

void thread_exit_current(void) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    current->status = TASK_DIED;
    if (current->in_ready)
        ready_remove(current);
    died_pending++;
    schedule();
    asm_restore_eflags(old);
}

void thread_kill_pid(uint32_t pid) {
    struct task_struct *t = NULL;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].slot_used && task_table[i].pid == pid) {
            t = &task_table[i];
            break;
        }
    }
    if (t == NULL || t->status == TASK_DIED || t->status == TASK_HANGING)
        return;
    if (t->pgdir == 0)
        return;

    uint32_t old = asm_save_eflags();
    asm_cli();
    t->exit_status = -1;
    t->status = TASK_HANGING;
    if (t->in_ready)
        ready_remove(t);

    /* 被杀线程的子进程成为孤儿, 无人 wait, 直接终止并回收 */
    kill_orphan_children((int32_t)t->pid);
    if (keyboard_ioq.consumer == t)
        keyboard_ioq.consumer = 0;
    if (keyboard_ioq.producer == t)
        keyboard_ioq.producer = 0;

    struct task_struct *parent = pid2thread(t->parent_pid);
    if (parent && parent->status == TASK_WAITING)
        thread_unblock(parent);

    if (t == current)
        schedule();
    asm_restore_eflags(old);
}

struct task_struct *pid2thread(int32_t pid) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].slot_used && (int32_t)task_table[i].pid == pid)
            return &task_table[i];
    }
    return NULL;
}

void thread_exit(struct task_struct *thread_over, int need_schedule) {
    (void)need_schedule;
    uint32_t old = asm_save_eflags();
    asm_cli();
    if (thread_over->status == TASK_DIED) {
        asm_restore_eflags(old);
        return;
    }
    thread_over->status = TASK_DIED;
    if (thread_over->in_ready)
        ready_remove(thread_over);
    died_pending++;
    asm_restore_eflags(old);
}

static void reap_died_threads(void) {
    struct list_elem *e = thread_all_list.head.next;
    while (e != &thread_all_list.tail) {
        struct task_struct *t = list_entry(e, struct task_struct, all_list_tag);
        struct list_elem *next = e->next;
        if (t->status == TASK_DIED && t != current) {
            if (t->pgdir) {
                pfree(&kernel_pool, t->pgdir);
                t->pgdir = 0;
            }
            if (t->kernel_stack_top) {
                uint8_t *stack_base =
                    (uint8_t *)t->kernel_stack_top - THREAD_STACK_SIZE;
                for (uint32_t i = 0; i < THREAD_STACK_SIZE / PAGE_SIZE; i++)
                    free_kernel_page((uint32_t)(stack_base + i * PAGE_SIZE));
                t->kernel_stack_top = 0;
            }
            list_remove(&t->all_list_tag);
            t->slot_used = 0;
            died_pending--;
        }
        e = next;
    }
}