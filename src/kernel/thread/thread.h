
#ifndef THREAD_H
#define THREAD_H
#include <stdint.h>
#include "../lib/list/list.h"
#include "../memory/pool/pool.h"
#include "../include/signal.h"
#include "./percpu.h"
#define THREAD_STACK_SIZE 0x4000
#define MAX_TASKS 64
#define STACK_MAGIC 0x19860726
#define MAX_FILES_OPEN_PER_PROC 8
typedef int32_t pid_t;
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED,
    TASK_STOPPED
};
typedef void (*thread_func)(void*);

struct thread_stack {
    uint64_t rflags;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    void (*rip)(void);
};
struct task_struct {
    uint64_t* self_kstack;
    enum task_status status;
    uint32_t pid;
    char name[16];
    uint8_t priority;
    uint8_t ticks;
    uint32_t elapsed_ticks;
    struct list_elem general_tag;
    struct list_elem all_list_tag;
    struct list_elem futex_tag;
    uint32_t futex_ready;
    int32_t parent_pid;
    int8_t exit_status;
    uint64_t kernel_stack_top;
    uint32_t pgdir;
    struct virtual_addr userprog_v_addr;
    uint32_t user_brk;
    uint32_t signal_pending;
    uint32_t signal_mask;
    struct sigaction sigactions[NSIG];
    uint32_t cwd_inode_nr;
    uint32_t fd_table[MAX_FILES_OPEN_PER_PROC];
    uint32_t tls_base;
    uint32_t tls_selector;
    int32_t  errno;
    uint32_t compat;
    uint32_t stack_magic;
};
extern struct task_struct* idle_thread;
extern struct list g_thread_all_list;
extern struct list g_ready_list;
extern uint32_t g_foreground_pid;
extern uint32_t g_init_pid;
void thread_init(void);
void kernel_thread(char* name, uint8_t priority, thread_func function, void* arg);
struct task_struct* thread_create(char* name, uint8_t priority, thread_func function, void* arg);
void schedule(void);
void switch_to(uint64_t** cur_kstack, uint64_t** next_kstack);
void kernel_thread_entry(void);
void thread_block(void);
void thread_unblock(struct task_struct* t);
void thread_yield(void);
void thread_block_with_status(enum task_status status);
struct task_struct* pid2thread(int32_t pid);
void thread_exit(struct task_struct* thread_over, int need_schedule);
typedef int (*thread_all_action)(struct task_struct*, void*);
int thread_traverse_all(thread_all_action action, void* arg);
struct task_struct* thread_alloc_slot(const char* name, uint8_t priority);
void thread_ready(struct task_struct* t);
void thread_exit_current(void);
void thread_kill_pid(uint32_t pid);
int thread_is_died(uint32_t pid);
typedef void (*fork_continuation)(void* arg, uint32_t child_pid, int is_child);
int thread_fork_with_cb(const char* name, uint8_t priority,
                        fork_continuation cb, void* arg);
#endif
