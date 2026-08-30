#ifndef POOL_H
#define POOL_H

#include "kernel/mm/bitmap/bitmap.h"
#include <stdint.h>

#define PAGE_SIZE 0x1000
#define MEMORY_BASE 0x100000
#define MAX_PHYS_MEM 0x20000000

struct pool {
    struct bitmap pool_bitmap;
    uint32_t phy_addr_start;
    uint32_t pool_size;
};

struct virtual_addr {
    struct bitmap vaddr_bitmap;
    uint32_t vaddr_start;
};

extern struct pool kernel_pool;
extern struct virtual_addr kernel_vaddr;
extern uint32_t kernel_kphys;

void pae_init(void);
void mm_init(void);
void *palloc(struct pool *pool);
void pfree(struct pool *pool, uint32_t phy_addr);
uint32_t palloc_pages(struct pool *pool, uint32_t cnt);

#define COW_FLAG (1u << 9)

int page_cow_resolve(uint32_t vaddr, uint64_t pte_val);

#define VIRT_OF(phys) ((phys) + 0xC0000000ull)

#define KERNEL_VADDR_START 0x40400000u
#define KERNEL_VADDR_SIZE  0x1000000u
#define PHY_OF(vaddr) ((uint32_t)((vaddr) - 0xC0000000ull))
#define PTE_PHYS(e) ((uint64_t)(e) & 0x000ffffffffff000ull)

/* 页面权限位 */
#define PTE_P   (1ull << 0)   /* present */
#define PTE_W   (1ull << 1)   /* writable */
#define PTE_U   (1ull << 2)   /* user */
#define PTE_NX  (1ull << 63)  /* no-execute */

/* NXE 读回验证通过才为 1; 否则 bit63 是保留位, 触发 RSVD #PF */
extern int g_nx_usable;

/* W^X: 可写则不可执行, 可执行则不可写, 否则只读 */
static inline uint64_t pte_wx(uint64_t base, int writable, int executable) {
    base &= ~PTE_W;
    if (writable) {
        base |= PTE_W;
    }
    if (!g_nx_usable) {
        base &= ~PTE_NX;  /* 无 NX 能力, 置位必 RSVD */
        return base;
    }
    if (executable && !writable) {
        base &= ~PTE_NX;
    } else {
        base |= PTE_NX;
    }
    return base;
}

void page_free_or_decref(uint32_t phy_addr);

uint64_t *pte_ptr(uint32_t vaddr);
uint64_t *pde_ptr(uint32_t vaddr);
void page_table_dump(uint32_t vaddr);
void *get_a_page(uint32_t vaddr);
void *get_kernel_pages(uint32_t pg_cnt);

void *ioremap(uint32_t phy_addr, uint32_t size);
void free_kernel_page(uint32_t vaddr);
void free_user_page(uint32_t vaddr);
uint64_t *phys_to_virt(uint64_t phys);

#endif
