#ifndef COREZ_LINUX_COMPAT_H
#define COREZ_LINUX_COMPAT_H

#include "kernel/asm/stub.h"
#include <stdint.h>

#define COMPAT_SYSCALL_BASE 0x50000

#include "kernel/syscall/linux_abi.h"

uint32_t linux_compat_handler(struct Registers *r);

#endif
