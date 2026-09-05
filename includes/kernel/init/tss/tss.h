#ifndef TSS_H
#define TSS_H

#include <stdint.h>

struct task_struct;

struct tss {
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist[7];
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed));

struct tss *tss_cpu(uint32_t idx);
void tss_init(void);
void tss_ap_init(uint32_t idx, uint32_t kstack_top);
void tss_update_rsp0(struct task_struct *task);

#endif
