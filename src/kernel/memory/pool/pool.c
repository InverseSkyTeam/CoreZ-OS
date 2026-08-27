#include "./pool.h"
#include "../../include/asmFunc.h"
#include "../../include/assert.h"
#include "../../initer/io/io.h"
#include "../../lib/str/str.h"
#include "../../thread/percpu.h"
#include "../../thread/sync.h"
#include "../../thread/thread.h"
static struct lock mem_lock;

#define PML4_INDEX(v) (((uint64_t)(v) >> 39) & 0x1ff)
#define PDPT_INDEX(v) (((uint64_t)(v) >> 30) & 0x1ff)
#define PD_INDEX(v) (((uint64_t)(v) >> 21) & 0x1ff)
#define PT_INDEX(v) (((uint64_t)(v) >> 12) & 0x1ff)

static uint8_t kernel_pool_bitmap[(MAX_PHYS_MEM - MEMORY_BASE) / PAGE_SIZE / 8];
static uint8_t kernel_vaddr_bitmap[0x1000000 / PAGE_SIZE / 8];
struct pool kernel_pool;
struct virtual_addr kernel_vaddr;
#define FRAME_IDX(phy) (((phy) - MEMORY_BASE) / PAGE_SIZE)
#define FRAME_IDX_MAX ((MAX_PHYS_MEM - MEMORY_BASE) / PAGE_SIZE)
static uint8_t frame_owner[FRAME_IDX_MAX];
static uint64_t kernel_pml4;
uint32_t kernel_kphys;

#define KERNEL_VADDR_START 0x40400000

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

#define EFER_MSR 0xc0000080u
#define EFER_NXE (1ull << 11)
#define CPUID_NX (1u << 20)

static int cpuid_has_nx(void) {
    uint32_t a = 0x80000001, d;
    __asm__ volatile("cpuid" : "+a"(a), "=d"(d) : : "ebx", "ecx");
    return (d & CPUID_NX) != 0;
}

int g_nx_usable = 0;

void pae_init(void) {
    /* 读回确认 NXE 生效; 未生效则 PTE bit63 是保留位, 触发 RSVD #PF */
    if (cpuid_has_nx()) {
        uint64_t efer = asm_rdmsr(EFER_MSR);
        asm_wrmsr(EFER_MSR, efer | EFER_NXE);
        g_nx_usable = (asm_rdmsr(EFER_MSR) & EFER_NXE) != 0;
    } else {
        g_nx_usable = 0;
    }
}

void mm_init(void) {
    pae_init();
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
    mark_used(kernel_kphys, 0x200000);
    mark_used(0x400000, 0x460000 - 0x400000);
    mark_used(PER_CPU_BASE, NR_CPU * PAGE_SIZE);
    kernel_vaddr.vaddr_start = KERNEL_VADDR_START;
    kernel_vaddr.vaddr_bitmap.bits = kernel_vaddr_bitmap;
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = sizeof(kernel_vaddr_bitmap);
    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    kernel_pml4 = asm_read_cr3();
    lock_init(&mem_lock);

    {
        uint64_t *pd98 = (uint64_t *)VIRT_OF(0x98000);
        pd98[4] = (uint64_t)0x00080000 | 0x83;
        pd98[7] = (uint64_t)0x000E0000 | 0x83;
    }
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

static uint64_t cur_pml4(void) {
    if (current && current->pgdir) {
        return (uint64_t)current->pgdir;
    }
    return asm_read_cr3();
}

static uint64_t *pte_query(uint64_t pml4_phys, uint64_t vaddr) {
    uint64_t *pml4 = (uint64_t *)VIRT_OF(pml4_phys);
    uint64_t e = pml4[PML4_INDEX(vaddr)];
    if (!(e & 1))
        return 0;
    uint64_t *pdp = (uint64_t *)VIRT_OF(PTE_PHYS(e));
    e = pdp[PDPT_INDEX(vaddr)];
    if (!(e & 1))
        return 0;
    uint64_t *pd = (uint64_t *)VIRT_OF(PTE_PHYS(e));
    e = pd[PD_INDEX(vaddr)];
    if (!(e & 1))
        return 0;
    uint64_t *pt = (uint64_t *)VIRT_OF(PTE_PHYS(e));
    return &pt[PT_INDEX(vaddr)];
}

static uint64_t *pte_make(uint64_t pml4_phys, uint64_t vaddr) {
    uint64_t *pml4 = (uint64_t *)VIRT_OF(pml4_phys);
    uint32_t idx = PML4_INDEX(vaddr);
    if (!(pml4[idx] & 1)) {
        uint32_t p = palloc_raw(&kernel_pool);
        if (p == 0)
            return 0;
        pml4[idx] = p | 7;
        memset((void *)VIRT_OF(p), 0, PAGE_SIZE);
    }
    uint64_t *pdp = (uint64_t *)VIRT_OF(PTE_PHYS(pml4[idx]));
    idx = PDPT_INDEX(vaddr);
    if (!(pdp[idx] & 1)) {
        uint32_t p = palloc_raw(&kernel_pool);
        if (p == 0)
            return 0;
        pdp[idx] = p | 7;
        memset((void *)VIRT_OF(p), 0, PAGE_SIZE);
    }
    uint64_t *pd = (uint64_t *)VIRT_OF(PTE_PHYS(pdp[idx]));
    idx = PD_INDEX(vaddr);
    if (!(pd[idx] & 1)) {
        uint32_t p = palloc_raw(&kernel_pool);
        if (p == 0)
            return 0;
        pd[idx] = p | 7;
        memset((void *)VIRT_OF(p), 0, PAGE_SIZE);
    }
    uint64_t *pt = (uint64_t *)VIRT_OF(PTE_PHYS(pd[idx]));
    return &pt[PT_INDEX(vaddr)];
}

static uint64_t pte_zero, pde_zero;

uint64_t *pde_ptr(uint32_t vaddr) {
    uint64_t pml4_phys = cur_pml4();
    uint64_t *pml4 = (uint64_t *)VIRT_OF(pml4_phys);
    uint64_t e = pml4[PML4_INDEX(vaddr)];
    if (!(e & 1))
        return &pde_zero;
    uint64_t *pdp = (uint64_t *)VIRT_OF(PTE_PHYS(e));
    e = pdp[PDPT_INDEX(vaddr)];
    pde_zero = e;
    return &pde_zero;
}

uint64_t *pte_ptr(uint32_t vaddr) {
    uint64_t *pte = pte_query(cur_pml4(), (uint64_t)vaddr);
    return pte ? pte : &pte_zero;
}

void page_table_dump(uint32_t vaddr) {
    uint64_t pml4_phys = cur_pml4();
    uint64_t *pml4 = (uint64_t *)VIRT_OF(pml4_phys);
    uint64_t e0 = pml4[PML4_INDEX(vaddr)];
    uint64_t e1 = 0, e2 = 0, e3 = 0;
    if (e0 & 1) {
        uint64_t *pdp = (uint64_t *)VIRT_OF(PTE_PHYS(e0));
        e1 = pdp[PDPT_INDEX(vaddr)];
        if (e1 & 1) {
            uint64_t *pd = (uint64_t *)VIRT_OF(PTE_PHYS(e1));
            e2 = pd[PD_INDEX(vaddr)];
            if ((e2 & 1) && !(e2 & (1ull << 7))) { /* 跳过 2M 大页 */
                uint64_t *pt = (uint64_t *)VIRT_OF(PTE_PHYS(e2));
                e3 = pt[PT_INDEX(vaddr)];
            }
        }
    }

    kprintf("  [pgtbl] nx_usable=%d efer=0x%x\n", g_nx_usable,
            (uint32_t)asm_rdmsr(EFER_MSR));
    kprintf("  [pgtbl] cr3=0x%x vaddr=0x%x\n", (uint32_t)pml4_phys, vaddr);
    kprintf("  [pgtbl] PML4[%d]=0x%x\n", (int)PML4_INDEX(vaddr), (uint32_t)e0);
    kprintf("  [pgtbl] PDPT[%d]=0x%x\n", (int)PDPT_INDEX(vaddr), (uint32_t)e1);
    kprintf("  [pgtbl] PD[%d]=0x%x\n", (int)PD_INDEX(vaddr), (uint32_t)e2);
    kprintf("  [pgtbl] PT[%d]=0x%x  (P=%d W=%d U=%d PCD=%d PAT=%d G=%d "
            "NX=%d phys=%#x)\n",
            (int)PT_INDEX(vaddr), (uint32_t)e3, (int)(e3 & 1),
            (int)((e3 >> 1) & 1), (int)((e3 >> 2) & 1), (int)((e3 >> 4) & 1),
            (int)((e3 >> 7) & 1), (int)((e3 >> 8) & 1), (int)((e3 >> 63) & 1),
            (uint32_t)(e3 & 0x000ffffffffff000ull));
}

static void page_table_add_raw(uint32_t vaddr, uint32_t phy_addr) {
    uint64_t *pte = pte_make(cur_pml4(), (uint64_t)vaddr);
    if (pte == 0)
        return;
    *pte = (uint64_t)phy_addr | pte_wx(PTE_P | PTE_U, 1, 0);
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static void page_table_add_no_cache(uint32_t vaddr, uint32_t phy_addr) {
    uint64_t *pte = pte_make(kernel_pml4, (uint64_t)vaddr);
    if (pte == 0) {
        return;
    }
    /* MMIO: 可写、不可执行; PCD(bit4) 关闭缓存 */
    *pte = (uint64_t)phy_addr | pte_wx(PTE_P | PTE_U | 0x10, 1, 0);
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

uint64_t *phys_to_virt(uint64_t phys) {
    return (uint64_t *)(uintptr_t)VIRT_OF(phys);
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
    uint32_t phy = palloc_pages_raw(&kernel_pool, pg_cnt);
    if (phy == 0) {
        lock_release(&mem_lock);
        return 0;
    }
    for (uint32_t i = 0; i < pg_cnt; i++) {
        memset((void *)(VIRT_OF(phy) + i * PAGE_SIZE), 0, PAGE_SIZE);
    }
    lock_release(&mem_lock);
    return (void *)(uintptr_t)VIRT_OF(phy);
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
    lock_acquire(&mem_lock);
    uint32_t phy = vaddr - 0xC0000000u;
    pfree_raw(&kernel_pool, phy);
    lock_release(&mem_lock);
}

void free_user_page(uint32_t vaddr) {
    struct task_struct *cur = current;
    lock_acquire(&mem_lock);
    uint64_t *pte = pte_ptr(vaddr);
    if (*pte & 1) {
        uint32_t phy = (uint32_t)(*pte & 0xfffff000ull);
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
