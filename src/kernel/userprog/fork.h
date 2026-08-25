
#ifndef FORK_H
#define FORK_H

#include <stdint.h>
#include "../thread/thread.h"
#include "../include/asm/stub.h"

pid_t sys_fork(struct Registers* r);

#endif
