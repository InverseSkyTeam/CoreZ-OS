#include "interrupt.h"
#include "../../device/ide.h"
#include "../../device/keyboard.h"
#include "../../device/mouse.h"
#include "../../include/asm/stub.h"
#include "../../include/asmFunc.h"
#include "../../lib/str/str.h"
#include "../../memory/pool/pool.h"
#include "../../thread/thread.h"
#include "../../userprog/process.h"
#include "../apic/apic.h"
#include "../io/io.h"
#include "../pic/pic.h"
#include "../pit/pit.h"
volatile uint32_t g_tick = 0;
static const char *g_exc_names[32] = {"Divide Error",
                                      "Debug",
                                      "Non-Maskable Interrupt",
                                      "Breakpoint",
                                      "Overflow",
                                      "BOUND Range Exceeded",
                                      "Invalid Opcode",
                                      "Device Not Available",
                                      "Double Fault",
                                      "Coprocessor Seg Overrun",
                                      "Invalid TSS",
                                      "Segment Not Present",
                                      "Stack-Segment Fault",
                                      "General Protection",
                                      "Page Fault",
                                      "(Reserved)",
                                      "x87 FP Error",
                                      "Alignment Check",
                                      "Machine Check",
                                      "SIMD FP Exception",
                                      "Virtualization Exception",
                                      "Control Protection",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)",
                                      "(Reserved)"};
#define INT_NO_UNREGISTERED 0xFFFFu
static int handle_cow_fault(uint32_t fault_addr, uint32_t error_code) {
    if (fault_addr < USER_VADDR_START || fault_addr >= 0xc0000000) {
        return 0;
    }
    if (!(error_code & 0x2)) {
        return 0;
    }
    if (current == 0 || current->pgdir == 0) {
        return 0;
    }
    uint32_t *pte = pte_ptr(fault_addr);
    if (!(*pte & 1) || !(*pte & COW_FLAG)) {
        return 0;
    }
    uint32_t phy = *pte & 0xfffff000;
    if (!page_is_shared(phy)) {
        *pte = (*pte & ~(uint32_t)COW_FLAG) | 2;
        __asm__ volatile("invlpg (%0)" : : "r"(fault_addr) : "memory");
        return 1;
    }
    uint32_t new_phy = (uint32_t)palloc(&kernel_pool);
    if (new_phy == 0) {
        return 0;
    }
    memcpy((void *)new_phy, (void *)phy, PAGE_SIZE);
    *pte = (new_phy & 0xfffff000) | 7;
    __asm__ volatile("invlpg (%0)" : : "r"(fault_addr) : "memory");
    page_free_or_decref(phy);
    return 1;
}
void isr_handler(struct Registers *r) {
    uint32_t n = r->int_no;
    if (n == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        if (handle_cow_fault((uint32_t)cr2, r->err_code)) {
            return;
        }
    }
    if ((r->cs & 3) == 3) {
        int sig = exception_to_signal((int)n);
        if (sig > 0) {
            current->signal_pending |= (1u << sig);
            check_pending_signals(r);
            return;
        }
        setTextColor(12);
        kprintf("\n*** UNHANDLED USER EXCEPTION ***\n");
        if (n < 32) {
            kprintf("  %s (vector %d)  eip = 0x%x\n", g_exc_names[n], (int)n,
                    r->eip);
        }
        signal_terminate(current, SIGSEGV);
    }
    setTextColor(12);
    kprintf("\n*** EXCEPTION ***\n");
    if (n < 32) {
        kprintf("  %s (vector %d)\n", g_exc_names[n], (int)n);
        kprintf("  err_code = 0x%x\n", r->err_code);
    } else {
        kprintf("  Unregistered interrupt (vector %d)\n", (int)n);
    }
    kprintf("  eip = 0x%x  cs = 0x%x  eflags = 0x%x\n", r->eip, r->cs,
            r->eflags);
    kprintf("  eax = 0x%x  ebx = 0x%x  ecx = 0x%x  edx = 0x%x\n", r->eax,
            r->ebx, r->ecx, r->edx);
    kprintf("  esi = 0x%x  edi = 0x%x  ebp = 0x%x  rflags = 0x%x\n", r->esi,
            r->edi, r->ebp, r->rflags);
    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    kprintf("  cr2 (fault addr) = 0x%x\n", (uint32_t)cr2);
    if (cr2 >= 0xC1000000 && cr2 < 0xC1400000) {
        kprintf("  (note: fault is inside kernel virtual pool "
                "0xC1000000..0xC1400000)\n");
    } else if (cr2 >= 0xE0000000) {
        kprintf("  (note: fault is inside VRAM region 0xE0000000+)\n");
    }
    asm_cli();
    for (;;) {
        asm_hlt();
    }
}

static void irq_eoi(uint32_t irq) {
    if (apic_active()) {
        lapic_eoi();
    } else {
        pic_send_eoi((uint8_t)irq);
    }
}

void irq_handler(struct Registers *r) {
    uint32_t irq = r->int_no - 32;
    if (irq == 0) {
        irq_eoi(irq);
        g_tick++;
        if (current != 0) {
            check_pending_signals(r);
            schedule();
        }
        return;
    }
    if (irq == 1) {
        keyboard_handler();
    }
    if (irq == 12) {
        mouse_handler();
    }
    if (irq == 14) {
        intr_hd_handler((uint8_t)r->int_no);
    }
    irq_eoi(irq);
}
