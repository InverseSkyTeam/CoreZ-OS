// 参考: 《操作系统真相还原》(于渊) 第11章 输入输出系统
#include "./sync.h"

#include "../include/asmFunc.h"
#include "../include/assert.h"
#include "../initer/io/io.h"
#include "./thread.h"

void spinlock_init(struct spinlock *s) { s->locked = 0; }

void spinlock_acquire(struct spinlock *s) {
    while (asm_xchg(&s->locked, 1) != 0) {
        while (s->locked != 0) {
            asm_pause();
        }
    }

    __asm__ volatile("" : : : "memory");
}

void spinlock_release(struct spinlock *s) {
    __asm__ volatile("" : : : "memory");
    s->locked = 0;
}

void sema_init(struct semaphore *psema, uint8_t value) {
    psema->value = value;
    list_init(&psema->waiters);
    spinlock_init(&psema->lock);
}

void sema_down(struct semaphore *psema) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    spinlock_acquire(&psema->lock);
    while (psema->value == 0) {
        list_append(&psema->waiters, &current->general_tag);
        spinlock_release(&psema->lock);
        thread_block();
        spinlock_acquire(&psema->lock);
    }
    psema->value--;
    spinlock_release(&psema->lock);
    asm_restore_eflags(old);
}

void sema_up(struct semaphore *psema) {
    uint32_t old = asm_save_eflags();
    asm_cli();
    spinlock_acquire(&psema->lock);
    if (!list_empty(&psema->waiters)) {
        struct list_elem *e = list_pop_front(&psema->waiters);
        struct task_struct *w = list_entry(e, struct task_struct, general_tag);
        thread_unblock(w);
    }
    psema->value++;
    spinlock_release(&psema->lock);
    asm_restore_eflags(old);
}

void lock_init(struct lock *plock) {
    plock->holder = 0;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);
}

void lock_acquire(struct lock *plock) {
    if (plock->holder != current || plock->holder == 0) {
        uint32_t old_holder = (uint32_t)plock->holder;
        uint32_t sval = plock->semaphore.value;
        void *ret = __builtin_return_address(0);
        sema_down(&plock->semaphore);
        plock->holder = current;
        if (plock->holder_repeat_nr != 0) {
            asm_cli();
            kprintf("\n[LOCK ACQ BUG] plock=0x%x old_holder=0x%x cur=0x%x "
                    "rn=%d sval=%d "
                    "caller=0x%x\n",
                    (uint32_t)plock, old_holder, (uint32_t)current,
                    plock->holder_repeat_nr, sval, (uint32_t)ret);
            for (;;)
                asm_hlt();
        }
        plock->holder_repeat_nr = 1;
    } else {
        plock->holder_repeat_nr++;
    }
}

void lock_release(struct lock *plock) {
    if (plock->holder != current) {
        asm_cli();
        kprintf("\n[LOCK REL BUG] plock=0x%x holder=0x%x cur=0x%x rn=%d "
                "caller=0x%x\n",
                (uint32_t)plock, (uint32_t)plock->holder,
                (uint32_t)current, plock->holder_repeat_nr,
                (uint32_t)__builtin_return_address(0));
        for (;;)
            asm_hlt();
    }
    if (plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = 0;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}
