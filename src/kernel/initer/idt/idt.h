#include <stdint.h>

// 64 位 IDT 条目 (16 字节)
struct IDTEntry {
    uint16_t offset_low;    // offset[15:0]
    uint16_t selector;      // 代码段选择子 (SELECTOR_KERNEL_CODE=0x08)
    uint8_t  ist;
    uint8_t  type_attr;     // 0x8E: 64-bit 中断门, 0x8F: 64-bit 陷阱门, 0xEF: DPL3
    uint16_t offset_mid;    // offset[31:16]
    uint32_t offset_high;   // offset[63:32]
    uint32_t reserved;
};

#define IDT_TYPE_INT_GATE64   0x8E
#define IDT_TYPE_TRAP_GATE64  0x8F
#define IDT_TYPE_TRAP_GATE3   0xEF

extern struct IDTEntry idt[256];

void initIDT(void);