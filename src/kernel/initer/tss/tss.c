#include "tss.h"

#include "../../include/asmFunc.h"
#include "../../memory/pool/pool.h"
#include "../../thread/thread.h"
#include "../gdt/gdt.h"

struct tss tss;

void update_tss_esp(struct task_struct *pthread) {
    tss.rsp0 = (uint64_t)pthread->kernel_stack_top;
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
    tss.iomap_base = (uint16_t)sizeof(tss); // 置为 TSS 大小 -> 空 IO 位图

    set_tss_desc((uint64_t)&tss, sizeof(tss) - 1);
    asm_ltr(SELECTOR_TSS);
}