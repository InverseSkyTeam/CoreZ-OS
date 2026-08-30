#include "kernel/mm/access.h"
#include "lib/str/str.h"
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
        return 0;
    }
    if (n > (size_t)(USER_SPACE_END - a)) {
        return 0;
    }
    return 1;
}
