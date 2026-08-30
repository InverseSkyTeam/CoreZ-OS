#include "kernel/gui/shm.h"

#include "lib/str/str.h"
#include "kernel/mm/pool/pool.h"
#include "kernel/sched/sync.h"

#define SHM_MAX_POOLS 32

static struct shm_pool pools[SHM_MAX_POOLS];

static struct lock shm_lock;

void shm_init(void) {
    lock_init(&shm_lock);
}

struct shm_pool *shm_pool_create(uint32_t size) {
    if (size == 0)
        return 0;
    lock_acquire(&shm_lock);
    for (int i = 0; i < SHM_MAX_POOLS; i++) {
        if (pools[i].in_use)
            continue;
        uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint8_t *mem = (uint8_t *)get_kernel_pages(pages);
        if (mem == 0) {
            lock_release(&shm_lock);
            return 0;
        }
        memset(mem, 0, pages * PAGE_SIZE);
        pools[i].data = mem;
        pools[i].size = size;
        pools[i].pages = pages;
        pools[i].in_use = 1;
        lock_release(&shm_lock);
        return &pools[i];
    }
    lock_release(&shm_lock);
    return 0;
}

void shm_pool_destroy(struct shm_pool *pool) {
    if (pool == 0 || !pool->in_use)
        return;
    lock_acquire(&shm_lock);
    if (pool->in_use) {
        for (uint32_t i = 0; i < pool->pages; i++) {
            free_kernel_page((uint32_t)pool->data + i * PAGE_SIZE);
        }
        pool->data = 0;
        pool->size = 0;
        pool->pages = 0;
        pool->in_use = 0;
    }
    lock_release(&shm_lock);
}
