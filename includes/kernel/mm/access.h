/* 用户指针校验: 封装 access_ok / copy_from_user / copy_to_user. */
#ifndef MEMORY_ACCESS_H
#define MEMORY_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#define USER_SPACE_END 0xc0000000u

int access_ok(const void *addr, size_t n, int write);
int user_range_writable(uint32_t addr, uint32_t len);

#endif