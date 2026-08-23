#ifndef PERCPU_H
#define PERCPU_H
#include <stdint.h>
#define PER_CPU_BASE 0x00100000u
#define NR_CPU 4
struct task_struct;
struct percpu_data {
    struct task_struct *current_task;
};
#define SELECTOR_PER_CPU 0x38
static inline struct task_struct *get_current(void) {
    struct task_struct *cur;
    __asm__ volatile("movl %%gs:0, %0" : "=r"(cur) : : "memory");
    return cur;
}
static inline void set_current(struct task_struct *t) {
    __asm__ volatile("movl %0, %%gs:0" : : "r"(t) : "memory");
}
#define current get_current()
void percpu_init(void);
#endif
