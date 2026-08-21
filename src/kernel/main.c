#include "./include/asmFunc.h"
#include "./include/assert.h"
#include "./initer/pic/pic.h"
#include "./initer/pit/pit.h"
#include "./initer/io/io.h"
#include "./initer/idt/idt.h"
#include "./initer/gdt/gdt.h"
#include "./initer/tss/tss.h"
#include "./memory/pool/pool.h"
#include "./thread/thread.h"
#include "./device/keyboard.h"
#include "./device/mouse.h"
#include "./device/ide.h"
#include "./fs/fs.h"
#include "./userprog/process.h"
#include "./userprog/exec.h"
#include "./syscall/syscall.h"
#include "./syscall/futex.h"
#include "./shell/shell.h"
#include "./lib/user/syscall.h"
#include "./lib/user/stdio.h"
#include "./net/nt_net.h"

struct BootInfo {
    uint8_t  cyls;
    uint8_t  leds;
    uint8_t  vmode;
    uint8_t  _pad;
    uint16_t scrnx;
    uint16_t scrny;
    uint32_t vram;
    uint32_t vram_bytes;   
};

static void k_thread_a(void* arg) {
    for (;;) {
        thread_yield();
    }
}

static void k_thread_b(void* arg) {
    for (;;) {
        thread_yield();
    }
}

static void init(void) {
    g_init_pid = getpid();
    uint32_t ret_pid = fork();
    if (ret_pid > 0) {
        for (;;) {
            int32_t status = 0;
            int32_t child_pid = wait(&status);
            if (child_pid != -1) {
                printf("init: reaped child %d, status %d\n", (int)child_pid, (int)status);
            } else {
                thread_yield();
            }
        }
    } else if (ret_pid == 0) {
        /* 参考: exec.c 用户程序加载; 以用户态 nr_micro_shell 作为系统 shell */
        my_shell(NULL);
    } else {
        printf("init: fork failed\n");
        for (;;) {
        }
    }
}

void KMain(void) {
    const struct BootInfo *bootInfo = (const struct BootInfo*)0x0FF0;
    initPalette();
    initIO((uint8_t*)bootInfo->vram, bootInfo->scrnx, bootInfo->scrny, bootInfo->vram_bytes);
    initIDT();
    syscall_init();
    futex_init();
    mm_init();
    gdt_init();
    tss_init();
    setTextColor(10);
    printf("[OK] TSS loaded, TR=0x%x esp0=0x%x\n", (uint32_t)asm_str(), tss.esp0);

    setCursor(0, 0);

    setTextColor(14);
    printf("NiTianOS Kernel Inited.\n");

    setTextColor(10);
    printf("[OK] Higher Half Kernel @ 0xC0000000+\n");

    if (initPic() == 0) {
        printf("[OK] PIC inited\n");
    } else {
        setTextColor(12);
        printf("[FAIL] PIC init error\n");
    }

    initPIT(PIT_HZ);

    keyboard_init();
    mouse_init();
    thread_init();
    setTextColor(10);
    printf("[OK] thread mgr ready\n");

    kernel_thread("k_a", 4, k_thread_a, 0);
    kernel_thread("k_b", 4, k_thread_b, 0);

    asm_sti();

    ide_init();
    filesys_init();

    setTextColor(10);
    printf("[OK] user programs on ext2\n");

    net_init();  

    process_execute(init, "init");
    for (;;) {
        thread_yield();
        asm_hlt();
    }
}
