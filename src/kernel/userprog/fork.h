#ifndef FORK_H
#define FORK_H

#include "../include/asm/stub.h"
#include "../thread/thread.h"
#include <stdint.h>

pid_t sys_fork(struct Registers *r);

#endif
