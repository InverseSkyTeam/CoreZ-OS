#include "kernel/init/tss/tss.h"

#include "kernel/asmFunc.h"
#include "kernel/mm/pool/pool.h"
#include "kernel/sched/thread.h"
#include "kernel/init/gdt/gdt.h"

struct tss tss;

uint64_t syscall_kstack_top_data = 0;

void tss_update_rsp0(struct task_struct *task) {
    tss.rsp0 = (uint64_t)task->kernel_stack_top;
    syscall_kstack_top_data = (uint64_t)task->kernel_stack_top;
}

void tss_init(void) {
    uint32_t kstack = (uint32_t)palloc(&kernel_pool);

    tss.reserved1 = 0;
    tss.rsp0 = (uint64_t)kstack + PAGE_SIZE;
    tss.rsp1 = 0;
    tss.rsp2 = 0;
    tss.reserved2 = 0;
    for (int i = 0; i < 7; i++) {
        tss.ist[i] = 0;
    }
    tss.reserved3 = 0;
    tss.reserved4 = 0;
    tss.iomap_base = (uint16_t)sizeof(tss); /* 置为 TSS 大小 -> 空 IO 位图 */

    set_tss_desc((uint64_t)&tss, sizeof(tss) - 1);
    asm_ltr(SELECTOR_TSS);
}