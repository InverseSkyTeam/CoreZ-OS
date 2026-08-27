#include "./device/ide.h"
#include "./device/keyboard.h"
#include "./device/mouse.h"
#include "./fs/fs.h"
#include "./include/asmFunc.h"
#include "./include/assert.h"
#include "./initer/acpi/acpi.h"
#include "./initer/apic/apic.h"
#include "./initer/gdt/gdt.h"
#include "./initer/idt/idt.h"
#include "./initer/idt/interrupt.h"
#include "./initer/io/io.h"
#include "./initer/pic/pic.h"
#include "./initer/pit/pit.h"
#include "./initer/smp/smp.h"
#include "./initer/tss/tss.h"
#include "./lib/user/stdio.h"
#include "./lib/user/syscall.h"
#include "./memory/pool/pool.h"
#include "./net/nt_net.h"
#include "./shell/shell.h"
#include "./syscall/futex.h"
#include "./syscall/syscall.h"
#include "./thread/thread.h"
#include "./userprog/exec.h"
#include "./userprog/process.h"

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
    uint8_t *p = (uint8_t *)mbi_ptr + 8; /* 跳过 8 字节头部 */
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

static void init(void) {
    init_pid = getpid();
    uint32_t ret_pid = fork();
    if (ret_pid > 0) {
        for (;;) {
            int32_t status = 0;
            int32_t child_pid = wait(&status);
            if (child_pid != -1) {
                kprintf("init: reaped child %d, status %d\n", (int)child_pid,
                        (int)status);
            } else {
                thread_yield();
            }
        }
    } else if (ret_pid == 0) {
        my_shell(NULL);
    } else {
        kprintf("init: fork failed\n");
        for (;;) {
        }
    }
}
void kmain(uint32_t magic, void *mbi_ptr, uint32_t kphys) {
    asm_write_cr4(asm_read_cr4() | 0x600);
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

    kprintf("[init] keyboard\n");
    keyboard_init();
    mouse_init();

    kprintf("[init] threads\n");
    thread_init();
    kprintf("[OK] kernel threads ready\n");

    set_text_color(10);
    kprintf("[OK] kernel init done, enable IRQs\n");

    ide_init();
    filesys_init();
    smp_init();
    net_init();
    kernel_thread("shell", 4, my_shell, 0);
    for (;;) {
        cpu_idle();
        thread_yield();
    }
}
