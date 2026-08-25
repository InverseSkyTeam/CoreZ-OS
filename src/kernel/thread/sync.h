
#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include "../lib/list/list.h"









struct spinlock {
    volatile uint32_t locked;   
};

struct semaphore {
    uint8_t value;
    struct list waiters;
    
    
    
    struct spinlock lock;
};

struct lock {
    struct task_struct* holder;
    struct semaphore semaphore;
    uint32_t holder_repeat_nr;
};

void spinlock_init(struct spinlock* s);
void spinlock_acquire(struct spinlock* s);
void spinlock_release(struct spinlock* s);

void sema_init(struct semaphore* psema, uint8_t value);
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
void lock_init(struct lock* plock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);

#endif
