
#ifndef GDT_H
#define GDT_H
#include <stdint.h>

// 64 位 GDT 布局, 0x08/0x10/0x18 与 loader.asm GDT64 保持一致
// idx0 sel0x00 null
// idx1 sel0x08 kernel code  (64-bit, L=1)
// idx2 sel0x10 kernel data
// idx3 sel0x18 kernel compat code (32-bit D=1)
// idx4 sel0x23 user data  (DPL3, RPL3)
// idx5 sel0x2B user code  (32-bit compat, DPL3, RPL3)
// idx6 sel0x33 user code  (64-bit, DPL3, RPL3)
// idx7 sel0x38 TLS
// idx8 sel0x40 percpu
// idx9/10 sel0x48 TSS (16 字节描述符)
#define SELECTOR_KERNEL_CODE   0x08
#define SELECTOR_KERNEL_DATA   0x10
#define SELECTOR_KCOMPAT_CODE  0x18
#define SELECTOR_U_DATA        0x23
#define SELECTOR_U_CODE        0x2B
#define SELECTOR_USER64_CODE   0x33
#define SELECTOR_TLS           0x38
#define SELECTOR_PER_CPU       0x40
#define SELECTOR_TSS           0x48

#define GDT_TLS_INDEX      7
#define GDT_PER_CPU_INDEX  8
#define GDT_TSS_INDEX      9
#define GDT_ENTRIES        12

struct gdt_desc {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  attr_low;
    uint8_t  limit_high_attr_high;
    uint8_t  base_high;
} __attribute__((packed));

extern struct gdt_desc gdt[GDT_ENTRIES];

void gdt_init(void);
void set_tss_desc(uint64_t tss_base, uint32_t tss_limit);
void tls_desc_set_base(uint32_t base);
#endif