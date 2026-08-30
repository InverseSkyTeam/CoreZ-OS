#ifndef PROCESS_H
#define PROCESS_H

#include "kernel/sched/thread.h"
#include <stdint.h>

#define USER_VADDR_START 0x8048000
#define USER_STACK3_VADDR (0xc0000000 - 0x1000)
#define USER_HEAP_BASE 0xA0000000
#define USER_HEAP_LIMIT (USER_STACK3_VADDR - PAGE_SIZE)
#define DEFAULT_PRIO 15

extern void intr_exit(void);

void start_process(void *arg);
void page_dir_activate(struct task_struct *pthread);
void process_activate(struct task_struct *pthread);
uint32_t *create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct *user_prog);
void process_execute(char *path, char *name);

#endif
