// 用户指针校验: 封装 access_ok / copy_from_user / copy_to_user.
#ifndef MEMORY_ACCESS_H
#define MEMORY_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#define USER_SPACE_END 0xc0000000u

int access_ok(const void *addr, size_t n, int write);
size_t copy_from_user(void *dst, const void *user_src, size_t n);
size_t copy_to_user(void *user_dst, const void *src, size_t n);
size_t user_strnlen(const char *user, size_t maxlen);

#endif 