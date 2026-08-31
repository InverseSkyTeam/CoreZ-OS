#ifndef TSS_H
#define TSS_H

#include <stdint.h>

struct task_struct;

/* 64 位 TSS (共 104 字节 / 0x68) */
struct tss {
    uint32_t reserved1;  /* 0x00 */
    uint64_t rsp0;       /* 0x04 */
    uint64_t rsp1;       /* 0x0C */
    uint64_t rsp2;       /* 0x14 */
    uint64_t reserved2;  /* 0x1C */
    uint64_t ist[7];     /* 0x24 - 中断栈表 */
    uint64_t reserved3;  /* 0x5C */
    uint16_t reserved4;  /* 0x64 */
    uint16_t iomap_base; /* 0x66 */
} __attribute__((packed));

void tss_init(void);
void tss_update_rsp0(struct task_struct *task);

extern struct tss tss;

#endif