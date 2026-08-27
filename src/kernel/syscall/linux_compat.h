/* "unterschied/licenses/GPL-2.0" (仅参考流程) */
#ifndef NITIAN_LINUX_COMPAT_H
#define NITIAN_LINUX_COMPAT_H

#include "../include/asm/stub.h"
#include <stdint.h>

/* custom libc (lc.h) syscalls base, int 0x80 ABI */
#define COMPAT_SYSCALL_BASE 0x50000

/* Linux x86_64 syscall numbers (musl) */
#define SYS_LINUX_read            0
#define SYS_LINUX_write           1
#define SYS_LINUX_open            2
#define SYS_LINUX_close           3
#define SYS_LINUX_stat            4
#define SYS_LINUX_fstat           5
#define SYS_LINUX_lstat           6
#define SYS_LINUX_lseek           8
#define SYS_LINUX_mmap            9
#define SYS_LINUX_mprotect        10
#define SYS_LINUX_munmap          11
#define SYS_LINUX_brk             12
#define SYS_LINUX_rt_sigaction    13
#define SYS_LINUX_rt_sigprocmask  14
#define SYS_LINUX_pread64         17
#define SYS_LINUX_pwrite64        18
#define SYS_LINUX_readv           19
#define SYS_LINUX_writev          20
#define SYS_LINUX_access          21
#define SYS_LINUX_sched_yield     24
#define SYS_LINUX_nanosleep       35
#define SYS_LINUX_getpid          39
#define SYS_LINUX_exit            60
#define SYS_LINUX_kill            62
#define SYS_LINUX_fcntl           72
#define SYS_LINUX_getcwd          79
#define SYS_LINUX_chdir           80
#define SYS_LINUX_rename          82
#define SYS_LINUX_mkdir           83
#define SYS_LINUX_rmdir           84
#define SYS_LINUX_unlink          87
#define SYS_LINUX_readlink        89
#define SYS_LINUX_chmod           90
#define SYS_LINUX_chown           92
#define SYS_LINUX_gettimeofday    96
#define SYS_LINUX_getuid          102
#define SYS_LINUX_getgid          104
#define SYS_LINUX_geteuid         107
#define SYS_LINUX_getegid         108
#define SYS_LINUX_getppid         110
#define SYS_LINUX_futex           202
#define SYS_LINUX_set_tid_address 218
#define SYS_LINUX_clock_gettime   227
#define SYS_LINUX_clock_getres    228
#define SYS_LINUX_clock_nanosleep 230
#define SYS_LINUX_exit_group      231
#define SYS_LINUX_set_thread_area 234

uint32_t linux_compat_handler(struct Registers *r);

#endif
