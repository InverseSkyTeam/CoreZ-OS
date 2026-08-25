#include "./device/ide.h"
#include "./device/keyboard.h"
#include "./device/mouse.h"
#include "./fs/fs.h"
#include "./include/asmFunc.h"
#include "./include/assert.h"
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
struct BootInfo {
    uint8_t cyls;
    uint8_t leds;
    uint8_t vmode;
    uint8_t _pad;
    uint16_t scrnx;
    uint16_t scrny;
    uint32_t vram;
    uint32_t vram_bytes;
};
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
void kmain(void) {
    asm_write_cr4(asm_read_cr4() | 0x600);
    struct BootInfo *bi = (struct BootInfo *)0x0FF0;
    io_init((uint8_t *)(uintptr_t)VRAM_VIRT, bi->scrnx, bi->scrny,
            bi->vram_bytes);
    kprintf("[diag] vmode=%d scrnx=%d scrny=%d vram=0x%x bytes=%u\n",
            (int)bi->vmode, (int)bi->scrnx, (int)bi->scrny,
            (uint32_t)(uintptr_t)bi->vram, bi->vram_bytes);

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
        asm_sti();
        asm_hlt();
        thread_yield();
    }
}
