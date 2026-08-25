/* SMP 启动 */
#include "smp.h"

#include <stdint.h>

#include "../../include/asmFunc.h"
#include "../../initer/io/io.h"
#include "../../lib/str/str.h"
#include "../../memory/pool/pool.h"
#include "../../thread/percpu.h"
#include "../apic/apic.h"
#include "../gdt/gdt.h"

struct ap_boot_info {
    uint32_t gdtr;
    uint32_t stack_top;
    uint32_t ap_main;
    uint32_t index;
};

static struct gdt_desc ap_gdt[NR_CPU][GDT_ENTRIES];

struct cmp_gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
static struct cmp_gdtr ap_gdtr[NR_CPU];

static uint32_t ap_stack_top[NR_CPU];

static volatile uint32_t ap_ready[NR_CPU];

static void com1_putc(unsigned char c) {
    outb(0x3F8, c);
}
static void com1_str(const char *s) {
    while (*s)
        com1_putc((unsigned char)*s++);
}
static void com1_num(uint32_t v) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (v == 0) {
        com1_putc('0');
        return;
    }
    while (v) {
        buf[--i] = "0123456789"[v % 10];
        v /= 10;
    }
    com1_str(buf + i);
}

static void ap_desc_init(struct gdt_desc *d, uint32_t base, uint32_t limit,
                         uint8_t attr_low, uint8_t attr_high) {
    d->limit_low = limit & 0xFFFF;
    d->base_low = base & 0xFFFF;
    d->base_mid = (base >> 16) & 0xFF;
    d->attr_low = attr_low;
    d->limit_high_attr_high = ((limit >> 16) & 0x0F) | attr_high;
    d->base_high = (base >> 24) & 0xFF;
}

static void ap_build_gdt(uint32_t idx, uint32_t percpu_base) {
    struct gdt_desc *g = ap_gdt[idx];
    memset(g, 0, sizeof(ap_gdt[idx]));

    ap_desc_init(&g[1], 0, 0, 0x9A, 0x20);
    ap_desc_init(&g[2], 0, 0xFFFFF, 0x92, 0xCF);
    ap_desc_init(&g[GDT_PER_CPU_INDEX], percpu_base, 0xFFF, 0x92, 0x40);

    ap_gdtr[idx].limit = (uint16_t)(sizeof(ap_gdt[idx]) - 1);
    ap_gdtr[idx].base = (uint64_t)g;
}

static void ap_main(uint32_t idx) {
    set_current((struct task_struct *)0);

    com1_str("[AP] cpu");
    com1_num(idx);
    com1_str(" up\n");

    __asm__ volatile("" : : : "memory");
    ap_ready[idx] = 1;

    asm_cli();
    for (;;) {
        asm_hlt();
    }
}

static void smp_delay_busy(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        asm_pause();
    }
}

static void wakeup_ap(uint32_t idx) {
    volatile struct ap_boot_info *info =
        (volatile struct ap_boot_info *)AP_BOOT_INFO_ADDR;

    uint32_t percpu_base = PER_CPU_BASE + (uint32_t)idx * PAGE_SIZE;
    uint32_t stack = (uint32_t)palloc(&kernel_pool);
    if (stack == 0) {
        kprintf("[SMP] cpu%d: no page for stack, skip\n", (int)idx);
        return;
    }

    ap_build_gdt(idx, percpu_base);
    ap_stack_top[idx] = stack + PAGE_SIZE;
    ap_ready[idx] = 0;

    info->gdtr = (uint32_t)&ap_gdtr[idx];
    info->stack_top = ap_stack_top[idx];
    info->ap_main = (uint32_t)ap_main;
    info->index = idx;

    lapic_send_ipi_init((uint32_t)idx);
    smp_delay_busy(800000u);
    lapic_send_ipi_sipi((uint32_t)idx, AP_TRAMPOLINE_VECTOR);
    smp_delay_busy(250000u);
    lapic_send_ipi_sipi((uint32_t)idx, AP_TRAMPOLINE_VECTOR);

    uint32_t spins = 0;
    while (!ap_ready[idx]) {
        asm_pause();
        if (++spins > 1000000u)
            break;
    }
}

void smp_init(void) {
    uint32_t bsp_id = lapic_get_id();
    kprintf("[SMP] BSP LAPIC id=0x%x\n", (unsigned)bsp_id);
    com1_str("[SMP] smp_init enter\n");

    uint8_t *src = _binary_ap_trampoline_bin_start;
    uint32_t size = (uint32_t)(_binary_ap_trampoline_bin_end -
                               _binary_ap_trampoline_bin_start);
    if (size == 0 || size > 0x700) {
        kprintf("[SMP] no valid trampoline(%u), APs disabled\n",
                (unsigned)size);
        return;
    }
    memcpy((void *)AP_TRAMPOLINE_ADDR, src, size);

    uint32_t online = 1;
    for (uint32_t i = 1; i < NR_CPU; i++) {
        wakeup_ap(i);
        if (ap_ready[i]) {
            online++;
            kprintf("[SMP] cpu%d online (apic id=0x%x)\n", (int)i, (unsigned)i);
        } else {
            kprintf("[SMP] cpu%d no response, skip\n", (int)i);
        }
    }

    kprintf("[SMP] %u/%u CPU online\n", (unsigned)online, (unsigned)NR_CPU);
    com1_str("[SMP] online=");
    com1_num(online);
    com1_str("/");
    com1_num(NR_CPU);
    com1_str("\n");
}