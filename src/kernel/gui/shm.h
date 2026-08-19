#ifndef GUI_SHM_H
#define GUI_SHM_H

#include <stdint.h>
#include "../thread/sync.h"

struct shm_pool {
    uint8_t* data;
    uint32_t size;
    uint32_t pages;
    int in_use;
};

void shm_init(void);
struct shm_pool* shm_pool_create(uint32_t size);
void shm_pool_destroy(struct shm_pool* pool);

#endif
