#ifndef POOL_H
#define POOL_H

#include "../bitmap/bitmap.h"
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

#define VIRT_OF(phys) ((phys) + 0xC0000000ull)
#define PTE_PHYS(e) ((uint64_t)(e) & 0x000ffffffffff000ull)

/* 页面权限位 */
#define PTE_P   (1ull << 0)   /* present */
#define PTE_W   (1ull << 1)   /* writable */
#define PTE_U   (1ull << 2)   /* user */
#define PTE_NX  (1ull << 63)  /* no-execute */

/* W^X: 可写则不可执行，可执行则不可写，否则只读。base保留固有标志。 */
static inline uint64_t pte_wx(uint64_t base, int writable, int executable) {
    base &= ~PTE_W;
    if (writable) {
        base |= PTE_W;
    }
    if (executable && !writable) {
        base &= ~PTE_NX;
    } else {
        base |= PTE_NX;
    }
    return base;
}

void page_incr_shared(uint32_t phy_addr);
void page_free_or_decref(uint32_t phy_addr);
int page_is_shared(uint32_t phy_addr);

uint32_t *pte_ptr(uint32_t vaddr);
uint32_t *pde_ptr(uint32_t vaddr);
void page_table_add(uint32_t vaddr, uint32_t phy_addr);
void *get_a_page(uint32_t vaddr);
void *get_kernel_pages(uint32_t pg_cnt);

void *ioremap(uint32_t phy_addr, uint32_t size);
void free_kernel_page(uint32_t vaddr);
void free_user_page(uint32_t vaddr);
uint64_t *phys_to_virt(uint64_t phys);

#endif
