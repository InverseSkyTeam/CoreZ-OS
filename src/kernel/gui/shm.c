#include "./shm.h"
#include "../memory/pool/pool.h"
#include "../lib/str/str.h"
#include "../thread/sync.h"

#define SHM_MAX_POOLS 32

static struct shm_pool g_pools[SHM_MAX_POOLS];

static struct lock g_shm_lock;

void shm_init(void) {
    lock_init(&g_shm_lock);
}

struct shm_pool* shm_pool_create(uint32_t size) {
    if (size == 0) return 0;
    lock_acquire(&g_shm_lock);
    for (int i = 0; i < SHM_MAX_POOLS; i++) {
        if (g_pools[i].in_use) continue;
        uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint8_t* mem = (uint8_t*)get_kernel_pages(pages);
        if (mem == 0) {
            lock_release(&g_shm_lock);
            return 0;
        }
        memset(mem, 0, pages * PAGE_SIZE);
        g_pools[i].data = mem;
        g_pools[i].size = size;
        g_pools[i].pages = pages;
        g_pools[i].in_use = 1;
        lock_release(&g_shm_lock);
        return &g_pools[i];
    }
    lock_release(&g_shm_lock);
    return 0;
}

void shm_pool_destroy(struct shm_pool* pool) {
    if (pool == 0 || !pool->in_use) return;
    lock_acquire(&g_shm_lock);
    if (pool->in_use) {
        for (uint32_t i = 0; i < pool->pages; i++) {
            free_kernel_page((uint32_t)pool->data + i * PAGE_SIZE);
        }
        pool->data = 0;
        pool->size = 0;
        pool->pages = 0;
        pool->in_use = 0;
    }
    lock_release(&g_shm_lock);
}
