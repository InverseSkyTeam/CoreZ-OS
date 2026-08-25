
#include "gdt.h"
#include "../../include/asmFunc.h"
#include "../../thread/percpu.h"

struct gdt_desc gdt[GDT_ENTRIES];
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr0;

static void desc_init(struct gdt_desc *d, uint64_t base, uint32_t limit,
                      uint8_t attr_low, uint8_t attr_high) {
    d->limit_low = limit & 0xFFFF;
    d->base_low = base & 0xFFFF;
    d->base_mid = (base >> 16) & 0xFF;
    d->attr_low = attr_low;
    d->limit_high_attr_high = ((limit >> 16) & 0x0F) | attr_high;
    // 64 位模式下基址仅用低 32 位; 高 32 位由 TSS 描述符单独处理
    d->base_high = (base >> 24) & 0xFF;
}

// 64 位 TSS 硬描述符 (16 字节), 类型 0x89 (present, 64-bit TSS, busy=0)
void set_tss_desc(uint64_t tss_base, uint32_t tss_limit) {
    uint8_t *d = (uint8_t *)&gdt[GDT_TSS_INDEX];
    d[0] = tss_limit & 0xFF;
    d[1] = (tss_limit >> 8) & 0xFF;
    d[2] = tss_base & 0xFF;
    d[3] = (tss_base >> 8) & 0xFF;
    d[4] = (tss_base >> 16) & 0xFF;
    d[5] = 0x89; // access byte
    d[6] = (tss_base >> 24) & 0xFF;
    d[7] = ((tss_limit >> 16) & 0x0F); // G=0, AVL=0, limit[19:16]
    d[8] = (tss_base >> 32) & 0xFF;
    d[9] = (tss_base >> 40) & 0xFF;
    d[10] = (tss_base >> 48) & 0xFF;
    d[11] = (tss_base >> 56) & 0xFF;
    d[12] = 0;
    d[13] = 0;
    d[14] = 0;
    d[15] = 0;
}

void tls_desc_set_base(uint32_t base) {
    desc_init(&gdt[GDT_TLS_INDEX], base, 0xFFFFF, 0xF2, 0xCF);
}

void gdt_init(void) {
    desc_init(&gdt[0], 0, 0, 0, 0);             // null
    desc_init(&gdt[1], 0, 0, 0x9A, 0x20);       // kern code (L=1)
    desc_init(&gdt[2], 0, 0xFFFFF, 0x92, 0xCF); // kern data
    desc_init(&gdt[3], 0, 0xFFFFF, 0x9A, 0xCF); // kern compat code
    desc_init(&gdt[4], 0, 0xFFFFF, 0xF2, 0xCF); // user data
    desc_init(&gdt[5], 0, 0xFFFFF, 0xFA, 0xCF); // user code 32 compat
    desc_init(&gdt[6], 0, 0, 0xFA, 0x20);       // user code 64 (L=1)
    desc_init(&gdt[GDT_TLS_INDEX], 0, 0xFFFFF, 0xF2, 0xCF); // TLS (DPL3 data)
    desc_init(&gdt[GDT_PER_CPU_INDEX], PER_CPU_BASE, 0xFFF, 0x92,
              0x40); // percpu
    tls_desc_set_base(0);

    gdtr0.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr0.base = (uint64_t)gdt;
    asm_lgdt((uint64_t)&gdtr0);
}