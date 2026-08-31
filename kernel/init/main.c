#include "drivers/block/ide.h"
#include "drivers/char/keyboard.h"
#include "drivers/char/mouse.h"
#include "kernel/fs/fs.h"
#include "kernel/asmFunc.h"
#include "kernel/assert.h"
#include "kernel/init/acpi/acpi.h"
#include "kernel/init/apic/apic.h"
#include "drivers/driver_ops.h"
#include "kernel/init/gdt/gdt.h"
#include "arch/x86/interrupt/idt.h"
#include "arch/x86/interrupt/interrupt.h"
#include "drivers/char/console/io.h"
#include "kernel/init/pic/pic.h"
#include "kernel/init/pit/pit.h"
#include "kernel/init/smp/smp.h"
#include "kernel/init/tss/tss.h"
#include "libc/user/stdio.h"
#include "libc/user/syscall.h"
#include "kernel/mm/pool/pool.h"
#include "drivers/net/net.h"
#include "kernel/shell/shell.h"
#include "lib/rand/rand.h"
#include "kernel/syscall/futex.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/thread.h"
#include "kernel/userprog/exec.h"
#include "kernel/userprog/process.h"

#define VRAM_VIRT 0x80000000UL

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289UL
#define MULTIBOOT2_TAG_FRAMEBUFFER 8
#define MULTIBOOT2_TAG_END 0

struct mb2_tag_header {
    uint32_t type;
    uint32_t size;
};
struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t reserved;
    uint8_t color_info[6];
};

static void mb2_parse(uint32_t magic, void *mbi_ptr,
                      struct mb2_tag_framebuffer *fb) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || mbi_ptr == 0)
        return;
    uint8_t *p = (uint8_t *)mbi_ptr + 8; 
    uint32_t total = *(const uint32_t *)mbi_ptr;
    uint8_t *end = (uint8_t *)mbi_ptr + total;
    while (p + 8 <= end) {
        struct mb2_tag_header *tag = (struct mb2_tag_header *)p;
        if (tag->type == MULTIBOOT2_TAG_END)
            break;
        if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER && tag->size >= 24)
            *fb = *(struct mb2_tag_framebuffer *)p;
        p += (tag->size + 7) & ~7U;
    }
}

void drivers_init(int min_level, int max_level) {
    struct driver_ops table[16];
    int n = 0;
    for (const struct driver_ops *d = __drivers_start;
         d != __drivers_end && n < 16; ++d) {
        if (d->level < min_level || d->level > max_level)
            continue;
        int j = n;
        while (j > 0 && table[j - 1].level > d->level) {
            table[j] = table[j - 1];
            --j;
        }
        table[j] = *d;
        ++n;
    }
    for (int i = 0; i < n; ++i) {
        if (table[i].init)
            table[i].init();
    }
}

void kmain(uint32_t magic, void *mbi_ptr, uint32_t kphys) {
    asm_write_cr4(asm_read_cr4() | 0x600);
    asm_write_cr0(asm_read_cr0() | 0x10000);
    kernel_kphys = kphys;

    struct mb2_tag_framebuffer fb = {0};
    mb2_parse(magic, mbi_ptr, &fb);

    uint32_t bytes = (fb.framebuffer_pitch > 0)
                         ? fb.framebuffer_pitch * fb.framebuffer_height
                         : 0;
    io_init((uint8_t *)(uintptr_t)VRAM_VIRT, (int)fb.framebuffer_width,
            (int)fb.framebuffer_height, bytes);
    io_clear_screen();
    kprintf("[diag] magic=%#x mbi=%p fb: %ux%u bpp=%u addr=%#llx bytes=%u\n",
            magic, mbi_ptr, fb.framebuffer_width, fb.framebuffer_height,
            fb.framebuffer_bpp, (unsigned long long)fb.framebuffer_addr, bytes);

    kprintf("[init] mm\n");
    mm_init();

    kprintf("[init] gdt\n");
    gdt_init();

    kprintf("[init] percpu\n");
    percpu_init();
    set_current((struct task_struct *)0);

    kprintf("[init] tss\n");
    tss_init();

    kprintf("[init] idt\n");
    idt_init();

    kprintf("[init] syscall\n");
    syscall_init();
    futex_init();

    kprintf("[init] ppmode\n");
    kprintf("[OK] long mode (CR0.PG=1 CR4.PAE=1 EFER.LME=1 CS.L=1)\n");

    kprintf("[init] apic\n");
    if (apic_init() != 0) {
        kprintf("[WARN] apic_init failed, fallback PIC+PIT\n");
        pic_init();
        pit_init(PIT_HZ);
    }

    kprintf("[init] acpi\n");
    acpi_init();

    kprintf("[init] drivers char\n");
    drivers_init(0, 19);

    kprintf("[init] threads\n");
    thread_init();
    kprintf("[OK] kernel threads ready\n");

    set_text_color(10);
    kprintf("[OK] kernel init done, enable IRQs\n");

    drivers_init(20, 99);
    filesys_init();
    smp_init();
    net_check_guards();
    if (net_enable)
        net_init();

    rand_init();
    kernel_thread("shell", 4, my_shell, 0);
    for (;;) {
        net_check_guards();
        cpu_idle();
        thread_yield();
    }
}
