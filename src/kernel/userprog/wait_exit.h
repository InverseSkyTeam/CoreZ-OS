#ifndef WAIT_EXIT_H
#define WAIT_EXIT_H

#include "../thread/thread.h"
#include <stdint.h>

pid_t sys_wait(int32_t *status);
void sys_exit(int32_t status);

void proc_exit(struct task_struct *t, int status);

#endif
