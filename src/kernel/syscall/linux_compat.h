/* "unterschied/licenses/GPL-2.0" (仅参考流程) */
#ifndef NITIAN_LINUX_COMPAT_H
#define NITIAN_LINUX_COMPAT_H

#include "../include/asm/stub.h"
#include <stdint.h>

/* custom libc (lc.h) syscalls base, int 0x80 ABI */
#define COMPAT_SYSCALL_BASE 0x50000

#include "linux_abi.h"

uint32_t linux_compat_handler(struct Registers *r);

#endif
