// 参考: LDT/SetThreadArea (Linux fs/binfmt_elf.c, glibc nptl)
//      "unterschied/licenses/GPL-2.0" (仅参考流程)
#ifndef NITIAN_LINUX_COMPAT_H
#define NITIAN_LINUX_COMPAT_H

#include <stdint.h>
#include "../include/asm/stub.h"

#define COMPAT_SYSCALL_BASE 0x50000

enum linux_compat_nr {
    LC_PID            = COMPAT_SYSCALL_BASE + 0,
    LC_WRITE          = COMPAT_SYSCALL_BASE + 1,
    LC_READ           = COMPAT_SYSCALL_BASE + 2,
    LC_EXIT           = COMPAT_SYSCALL_BASE + 3,
    LC_BRK            = COMPAT_SYSCALL_BASE + 4,
    LC_OPEN           = COMPAT_SYSCALL_BASE + 5,
    LC_CLOSE          = COMPAT_SYSCALL_BASE + 6,
    LC_MMAP           = COMPAT_SYSCALL_BASE + 7,
    LC_SET_THREAD_AREA= COMPAT_SYSCALL_BASE + 8,
    LC_WRITEV         = COMPAT_SYSCALL_BASE + 9
};

uint32_t linux_compat_handler(struct Registers* r);

#endif