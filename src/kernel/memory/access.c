// 用户指针校验实现: access_ok / copy_from_user / copy_to_user / user_strnlen.
#include "./access.h"

#include "../lib/str/str.h"

#define USER_VADDR_BEGIN 0x8048000u

int access_ok(const void *addr, size_t n, int write) {
    (void)write;

    if (n == 0) {
        return 1; 
    }

    uint32_t a = (uint32_t)(uintptr_t)addr;

    if (a < USER_VADDR_BEGIN) {
        return 0; 
    }

    if (a >= USER_SPACE_END) {
        /* 内核高半区: 由内核自身传入的缓冲(如 shell 的 cmd_line), 属可信地址.
         * 内核未开 SMAP, 高半区页必定映射, 放行不会触发 #PF. */
        return 1;
    }

    /* 用户区 [USER_VADDR_BEGIN, USER_SPACE_END): 防整型回绕.
     * 先算用户上界与起始点的差值(必然为正), 再与长度比较;
     * n 大于该差值即说明 [addr, addr+n) 越出用户空间. */
    if (n > (size_t)(USER_SPACE_END - a)) {
        return 0;
    }
    return 1;
}

size_t copy_from_user(void *dst, const void *user_src, size_t n) {
    if (!access_ok(user_src, n, 0)) {
        return n;
    }
    memcpy(dst, user_src, n);
    return 0;
}

size_t copy_to_user(void *user_dst, const void *src, size_t n) {
    if (!access_ok(user_dst, n, 1)) {
        return n; 
    }
    memcpy(user_dst, src, n);
    return 0;
}

size_t user_strnlen(const char *user, size_t maxlen) {
    if (maxlen == 0) {
        return 0;
    }
    if (!access_ok(user, maxlen, 0)) {
        return maxlen;  
    }

    const char *p = user;
    const char *end = user + maxlen;
    while (p < end && *p != '\0') {
        p++;
    }
    return (size_t)(p - user);
}