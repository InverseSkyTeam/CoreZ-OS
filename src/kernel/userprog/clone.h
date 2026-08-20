// 参考: 《操作系统真相还原》(于渊) 第15章 fork + Linux clone(CLONE_VM)
#ifndef CLONE_H
#define CLONE_H

#include <stdint.h>
#include "../thread/thread.h"
#include "../include/asm/stub.h"

#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_THREAD    0x00010000
#define CLONE_SETTLS    0x00080000

pid_t sys_clone(struct Registers* r);

#endif