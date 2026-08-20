#include "./mmap.h"
#include "../userprog/process.h"
#include "../memory/pool/pool.h"
#include "../memory/bitmap/bitmap.h"
#include "../thread/thread.h"
#include "../lib/str/str.h"
#include "../include/assert.h"

static int32_t unmap_pages(uint32_t addr, uint32_t pages) {
    struct task_struct* cur = current_task;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t v = addr + i * PAGE_SIZE;
        free_user_page(v);
    }
    return 0;
}

static int page_is_mapped(uint32_t v) {
    uint32_t* pde = pde_ptr(v);
    if (!(*pde & 1)) {
        return 0;
    }
    if (*pde & 0x80) {
        return 1;
    }
    return (*pte_ptr(v) & 1) ? 1 : 0;
}

static uint32_t find_free_region(uint32_t pages) {
    struct task_struct* cur = current_task;
    uint32_t start = cur->userprog_v_addr.vaddr_start;
    uint32_t limit = USER_STACK3_VADDR;
    uint32_t run = 0;
    uint32_t base = 0;
    for (uint32_t v = start; v < limit; v += PAGE_SIZE) {
        if (page_is_mapped(v)) {
            run = 0;
            base = 0;
            continue;
        }
        if (run == 0) {
            base = v;
        }
        run++;
        if (run == pages) {
            return base;
        }
    }
    return 0;
}

uint32_t sys_mmap(const struct mmap_args* a) {
    if (a == 0) {
        return (uint32_t)-1;
    }
    uint32_t len = a->len;
    if (len == 0) {
        return (uint32_t)-1;
    }
    uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    struct task_struct* cur = current_task;

    if ((a->flags & MAP_FIXED) && (a->addr & (PAGE_SIZE - 1)) == 0 &&
        a->addr >= cur->userprog_v_addr.vaddr_start) {
        for (uint32_t i = 0; i < pages; i++) {
            uint32_t p = a->addr + i * PAGE_SIZE;
            if (get_a_page(p) == 0) {
                unmap_pages(a->addr, i);
                return (uint32_t)-1;
            }
        }
        return a->addr;
    }
    uint32_t base = find_free_region(pages);
    if (base == 0) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0; i < pages; i++) {
        if (get_a_page(base + i * PAGE_SIZE) == 0) {
            unmap_pages(base, i);
            return (uint32_t)-1;
        }
    }
    return base;
}

int32_t sys_munmap(uint32_t addr, uint32_t len) {
    if (addr == 0 || len == 0) {
        return -1;
    }
    if (addr & (PAGE_SIZE - 1)) {
        return -1;
    }
    uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    unmap_pages(addr, pages);
    return 0;
}

int32_t sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot) {
    if (addr & (PAGE_SIZE - 1)) {
        return -1;
    }
    if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t v = addr + i * PAGE_SIZE;
        if (!page_is_mapped(v)) {
            continue;
        }
        uint32_t* pte = pte_ptr(v);
        uint32_t new_pte = *pte & 0xfffff000u;
        if (prot != 0) {
            new_pte |= 1u;
        }
        new_pte |= 4u;
        if (prot & PROT_WRITE) {
            new_pte |= 2u;
        }
        *pte = new_pte;
        __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory");
    }
    return 0;
}