// 参考: 《操作系统真相还原》(于渊) 第8章 内存管理
#include "./pool.h"
#include "../../include/asmFunc.h"
#include "../../include/assert.h"
#include "../../lib/str/str.h"
#include "../../thread/sync.h"
#include "../../thread/thread.h"
#include "../../thread/percpu.h"
static struct lock mem_lock;
#define PDE_INDEX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_INDEX(addr) ((addr & 0x003ff000) >> 12)
static uint8_t kernel_pool_bitmap[(MAX_PHYS_MEM - MEMORY_BASE) / PAGE_SIZE / 8];
static uint8_t kernel_vaddr_bitmap[0x1000000 / PAGE_SIZE / 8];
struct pool kernel_pool;
struct virtual_addr kernel_vaddr;
#define FRAME_IDX(phy) (((phy) - MEMORY_BASE) / PAGE_SIZE)
#define FRAME_IDX_MAX ((MAX_PHYS_MEM - MEMORY_BASE) / PAGE_SIZE)
static uint8_t frame_owner[FRAME_IDX_MAX];
#define KERNEL_VADDR_START 0xC1000000
static uint32_t e820_mem_upper(void) {
    uint32_t count = *(uint32_t *)0x6000;
    uint8_t *p = (uint8_t *)0x6004;
    uint32_t upper = 0;
    uint32_t i;
    for (i = 0; i < count; i++) {
        uint64_t base = *(uint64_t *)p;
        uint64_t len = *(uint64_t *)(p + 8);
        uint32_t type = *(uint32_t *)(p + 16);
        if (type == 1 && (uint32_t)(base + len) > upper) {
            upper = (uint32_t)(base + len);
        }
        p += 24;
    }
    return upper;
}
static void mark_used(uint32_t start, uint32_t size) {
    uint32_t end = start + size;
    while (start < end) {
        uint32_t idx = (start - kernel_pool.phy_addr_start) / PAGE_SIZE;
        if (idx < kernel_pool.pool_bitmap.btmp_bytes_len * 8) {
            bitmap_set(&kernel_pool.pool_bitmap, idx, 1);
        }
        start += PAGE_SIZE;
    }
}
void mm_init(void) {
    uint32_t upper = e820_mem_upper();
    kernel_pool.phy_addr_start = MEMORY_BASE;
    if (upper <= MEMORY_BASE) {
        upper = MEMORY_BASE + 0x100000;
    }
    if (upper > MAX_PHYS_MEM) {
        upper = MAX_PHYS_MEM;
    }
    kernel_pool.pool_size = upper - MEMORY_BASE;
    kernel_pool.pool_bitmap.bits = kernel_pool_bitmap;
    kernel_pool.pool_bitmap.btmp_bytes_len = sizeof(kernel_pool_bitmap);
    bitmap_init(&kernel_pool.pool_bitmap);
    mark_used(0x280000, 0x400000 - 0x280000);
    mark_used(0x400000, 0x460000 - 0x400000);
    mark_used(PER_CPU_BASE, NR_CPU * PAGE_SIZE);
    kernel_vaddr.vaddr_start = KERNEL_VADDR_START;
    kernel_vaddr.vaddr_bitmap.bits = kernel_vaddr_bitmap;
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = sizeof(kernel_vaddr_bitmap);
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    lock_init(&mem_lock);
}
static uint32_t palloc_raw(struct pool *pool) {
    int idx = bitmap_scan(&pool->pool_bitmap, 1);
    if (idx == -1) {
        return 0;
    }
    bitmap_set(&pool->pool_bitmap, (uint32_t)idx, 1);
    return pool->phy_addr_start + (uint32_t)idx * PAGE_SIZE;
}
static void pfree_raw(struct pool *pool, uint32_t phy_addr) {
    if (phy_addr < pool->phy_addr_start) {
        return;
    }
    uint32_t idx = (phy_addr - pool->phy_addr_start) / PAGE_SIZE;
    bitmap_set(&pool->pool_bitmap, idx, 0);
}
static uint32_t palloc_pages_raw(struct pool *pool, uint32_t cnt) {
    int idx = bitmap_scan(&pool->pool_bitmap, cnt);
    if (idx == -1) {
        return 0;
    }
    for (uint32_t i = 0; i < cnt; i++) {
        bitmap_set(&pool->pool_bitmap, (uint32_t)idx + i, 1);
    }
    return pool->phy_addr_start + (uint32_t)idx * PAGE_SIZE;
}
uint32_t *pde_ptr(uint32_t vaddr) {
    return (uint32_t *)(0xfffff000 + PDE_INDEX(vaddr) * 4);
}
uint32_t *pte_ptr(uint32_t vaddr) {
    return (uint32_t *)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) +
                        PTE_INDEX(vaddr) * 4);
}
static void page_table_add_raw(uint32_t vaddr, uint32_t phy_addr) {
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);
    if (*pde & 1) {
        if (*pde & 0x80) {
            uint32_t pde_base = vaddr & 0xffc00000;
            uint32_t pde_phy = palloc_raw(&kernel_pool);
            if (pde_phy == 0)
                return;
            uint32_t *table = (uint32_t *)pde_phy;
            for (uint32_t i = 0; i < 1024; i++) {
                table[i] = (pde_base + i * PAGE_SIZE) | 7;
            }
            *pde = pde_phy | 7;
            *pte = phy_addr | 7;
            return;
        }
        ASSERT(!(*pte & 1));
        *pte = phy_addr | 7;
    } else {
        uint32_t pde_phy = palloc_raw(&kernel_pool);
        if (pde_phy == 0)
            return;
        *pde = pde_phy | 7;
        memset((void *)((uint32_t)pte & 0xfffff000), 0, PAGE_SIZE);
        ASSERT(!(*pte & 1));
        *pte = phy_addr | 7;
    }
}
static void page_table_add_no_cache(uint32_t vaddr, uint32_t phy_addr) {
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);
    if (*pde & 1) {
        if (*pde & 0x80) {
            uint32_t pde_base = vaddr & 0xffc00000;
            uint32_t pde_phy = palloc_raw(&kernel_pool);
            if (pde_phy == 0)
                return;
            uint32_t *table = (uint32_t *)pde_phy;
            for (uint32_t i = 0; i < 1024; i++) {
                table[i] = (pde_base + i * PAGE_SIZE) | 0x17;
            }
            *pde = pde_phy | 0x17;
            *pte = phy_addr | 0x17;
            return;
        }
        ASSERT(!(*pte & 1));
        *pte = phy_addr | 0x17;
    } else {
        uint32_t pde_phy = palloc_raw(&kernel_pool);
        if (pde_phy == 0)
            return;
        *pde = pde_phy | 0x17;
        memset((void *)((uint32_t)pte & 0xfffff000), 0, PAGE_SIZE);
        ASSERT(!(*pte & 1));
        *pte = phy_addr | 0x17;
    }
}
void *ioremap(uint32_t phy_addr, uint32_t size) {
    uint32_t phy = phy_addr & ~0xfff;
    uint32_t cnt = (phy_addr + size - 1) / PAGE_SIZE - phy / PAGE_SIZE + 1;
    lock_acquire(&mem_lock);
    int bit = bitmap_scan(&kernel_vaddr.vaddr_bitmap, cnt);
    if (bit == -1) {
        lock_release(&mem_lock);
        return 0;
    }
    uint32_t vaddr = kernel_vaddr.vaddr_start + (uint32_t)bit * PAGE_SIZE;
    for (uint32_t i = 0; i < cnt; i++) {
        bitmap_set(&kernel_vaddr.vaddr_bitmap, (uint32_t)bit + i, 1);
        page_table_add_no_cache(vaddr + i * PAGE_SIZE, phy + i * PAGE_SIZE);
    }
    lock_release(&mem_lock);
    return (void *)(vaddr + (phy_addr & 0xfff));
}
void *get_a_page(uint32_t vaddr) {
    struct task_struct *cur = current;
    uint32_t bit_idx = (vaddr - cur->userprog_v_addr.vaddr_start) / PAGE_SIZE;
    ASSERT(bit_idx < cur->userprog_v_addr.vaddr_bitmap.btmp_bytes_len * 8);
    lock_acquire(&mem_lock);
    bitmap_set(&cur->userprog_v_addr.vaddr_bitmap, bit_idx, 1);
    uint32_t phy = palloc_raw(&kernel_pool);
    if (phy == 0) {
        lock_release(&mem_lock);
        return 0;
    }
    page_table_add_raw(vaddr, phy);
    memset((void *)vaddr, 0, PAGE_SIZE);
    lock_release(&mem_lock);
    return (void *)vaddr;
}
void *get_kernel_pages(uint32_t pg_cnt) {
    lock_acquire(&mem_lock);
    int bit = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
    if (bit == -1) {
        lock_release(&mem_lock);
        return 0;
    }
    uint32_t vaddr = kernel_vaddr.vaddr_start + (uint32_t)bit * PAGE_SIZE;
    for (uint32_t i = 0; i < pg_cnt; i++) {
        bitmap_set(&kernel_vaddr.vaddr_bitmap, (uint32_t)bit + i, 1);
        uint32_t phy = palloc_raw(&kernel_pool);
        if (phy == 0) {
            for (uint32_t j = 0; j < i; j++) {
                uint32_t *pte = pte_ptr(vaddr + j * PAGE_SIZE);
                if (*pte & 1) {
                    pfree_raw(&kernel_pool, *pte & 0xfffff000);
                    *pte = 0;
                }
                bitmap_set(&kernel_vaddr.vaddr_bitmap, (uint32_t)bit + j, 0);
            }
            lock_release(&mem_lock);
            return 0;
        }
        page_table_add_raw(vaddr + i * PAGE_SIZE, phy);
        memset((void *)(vaddr + i * PAGE_SIZE), 0, PAGE_SIZE);
    }
    lock_release(&mem_lock);
    return (void *)vaddr;
}
void *palloc(struct pool *pool) {
    lock_acquire(&mem_lock);
    void *r = (void *)palloc_raw(pool);
    lock_release(&mem_lock);
    return r;
}
void pfree(struct pool *pool, uint32_t phy_addr) {
    lock_acquire(&mem_lock);
    pfree_raw(pool, phy_addr);
    lock_release(&mem_lock);
}
uint32_t palloc_pages(struct pool *pool, uint32_t cnt) {
    lock_acquire(&mem_lock);
    uint32_t r = palloc_pages_raw(pool, cnt);
    lock_release(&mem_lock);
    return r;
}
void page_table_add(uint32_t vaddr, uint32_t phy_addr) {
    lock_acquire(&mem_lock);
    page_table_add_raw(vaddr, phy_addr);
    lock_release(&mem_lock);
}
void free_kernel_page(uint32_t vaddr) {
    uint32_t old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm_write_cr3(0x400000);
    lock_acquire(&mem_lock);
    uint32_t *pde = pde_ptr(vaddr);
    if (!(*pde & 1) || (*pde & 0x80)) {
        lock_release(&mem_lock);
        asm_write_cr3(old_cr3);
        return;
    }
    uint32_t *pte = pte_ptr(vaddr);
    if (*pte & 1) {
        uint32_t phy = *pte & 0xfffff000;
        *pte = 0;
        pfree_raw(&kernel_pool, phy);
        uint32_t bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PAGE_SIZE;
        if (bit_idx < kernel_vaddr.vaddr_bitmap.btmp_bytes_len * 8) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 0);
        }
    }
    lock_release(&mem_lock);
    asm_write_cr3(old_cr3);
}
void free_user_page(uint32_t vaddr) {
    struct task_struct *cur = current;
    lock_acquire(&mem_lock);
    uint32_t *pte = pte_ptr(vaddr);
    if (*pte & 1) {
        uint32_t phy = *pte & 0xfffff000;
        *pte = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
        uint32_t bit_idx =
            (vaddr - cur->userprog_v_addr.vaddr_start) / PAGE_SIZE;
        if (bit_idx < cur->userprog_v_addr.vaddr_bitmap.btmp_bytes_len * 8) {
            bitmap_set(&cur->userprog_v_addr.vaddr_bitmap, bit_idx, 0);
        }
        lock_release(&mem_lock);
        page_free_or_decref(phy);
        return;
    }
    lock_release(&mem_lock);
}
void page_incr_shared(uint32_t phy_addr) {
    if (phy_addr < MEMORY_BASE || phy_addr >= MAX_PHYS_MEM) {
        return;
    }
    uint32_t idx = FRAME_IDX(phy_addr);
    lock_acquire(&mem_lock);
    if (frame_owner[idx] == 0) {
        frame_owner[idx] = 2;
    } else if (frame_owner[idx] < 0xFF) {
        frame_owner[idx]++;
    }
    lock_release(&mem_lock);
}
void page_free_or_decref(uint32_t phy_addr) {
    if (phy_addr < MEMORY_BASE || phy_addr >= MAX_PHYS_MEM) {
        return;
    }
    uint32_t idx = FRAME_IDX(phy_addr);
    lock_acquire(&mem_lock);
    if (frame_owner[idx] >= 1) {
        if (frame_owner[idx] > 1) {
            frame_owner[idx]--;
            lock_release(&mem_lock);
            return;
        }
        frame_owner[idx] = 0;
        lock_release(&mem_lock);
        pfree(&kernel_pool, phy_addr);
        return;
    }
    lock_release(&mem_lock);
    pfree(&kernel_pool, phy_addr);
}
int page_is_shared(uint32_t phy_addr) {
    if (phy_addr < MEMORY_BASE || phy_addr >= MAX_PHYS_MEM) {
        return 0;
    }
    uint32_t idx = FRAME_IDX(phy_addr);
    lock_acquire(&mem_lock);
    int shared = (frame_owner[idx] >= 1) ? 1 : 0;
    lock_release(&mem_lock);
    return shared;
}
