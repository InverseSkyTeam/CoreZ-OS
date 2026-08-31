#include "kernel/mm/access.h"
#include "kernel/mm/pool/pool.h"
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

static int user_range_walk(uint32_t addr, uint32_t len, int write) {
    if (addr < USER_VADDR_BEGIN || len == 0 ||
        len > USER_SPACE_END - USER_VADDR_BEGIN ||
        addr > USER_SPACE_END - len) {
        return 0;
    }
    uint32_t first = addr & ~0xFFFu;
    uint32_t last = (addr + len - 1) & ~0xFFFu;
    for (uint32_t p = first;; p += PAGE_SIZE) {
        uint64_t *pde = pde_ptr(p);
        if (pde == NULL || !(*pde & 1)) {
            return 0;
        }
        if (*pde & 0x80) {
            uint64_t need = PTE_U | (write ? PTE_W : 0);
            if ((*pde & need) != need) {
                return 0;
            }
        } else {
            uint64_t *pte = pte_ptr(p);
            if (!(*pte & 1) || !(*pte & PTE_U)) {
                return 0;
            }
            if (write && !(*pte & PTE_W) &&
                (!(*pte & COW_FLAG) || !page_cow_resolve(p, *pte))) {
                return 0;
            }
        }
        if (p == last) {
            return 1;
        }
    }
}
int user_range_readable(uint32_t addr, uint32_t len) {
    return user_range_walk(addr, len, 0);
}
int user_range_writable(uint32_t addr, uint32_t len) {
    return user_range_walk(addr, len, 1);
}
