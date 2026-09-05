#ifndef EXEC_H
#define EXEC_H

#include <stdint.h>

struct Registers;
int32_t sys_execv(const char *path, const char *argv[], struct Registers *regs);
int32_t sys_execve(const char *path, const char *argv[], const char *envp[],
                   struct Registers *regs);

#endif
