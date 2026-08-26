#include "./process.h"
#include "../include/asm/stub.h"
#include "../include/asmFunc.h"
#include "../include/assert.h"
#include "../initer/gdt/gdt.h"
#include "../initer/io/io.h"
#include "../initer/tss/tss.h"
#include "../lib/str/str.h"
#include "../memory/bitmap/bitmap.h"
#include "../memory/pool/pool.h"
#include "./exec.h"
#define DIV_ROUND_UP(X, STEP) ((X + STEP - 1) / STEP)
#define EFLAGS_MBS (1 << 1)
#define EFLAGS_IF_1 (1 << 9)
#define EFLAGS_IOPL_0 0

void start_process(void *arg) {
    char *path = (char *)arg;
    if (current->pgdir != 0) {
        process_activate(current);
    }
    const char *argv[] = {path, (const char *)0};
    if (sys_execv(path, argv, NULL) == -1) {
        kprintf("[exec] process_execute: load '%s' failed\n", path);
    }
    thread_exit_current();
}
void page_dir_activate(struct task_struct *pthread) {
    if (pthread->pgdir == 0)
        return;
    asm_write_cr3((uint64_t)pthread->pgdir);
}
void process_activate(struct task_struct *pthread) {
    if (pthread->pgdir != 0) {
        page_dir_activate(pthread);
        if (pthread->tls_selector != 0) {
            tls_desc_set_base(pthread->tls_base);
        }
        update_tss_esp(pthread);
    }
}

uint32_t *create_page_dir(void) {
    uint64_t pml4_phys = palloc_pages(&kernel_pool, 1);
    if (pml4_phys == 0) {
        return 0;
    }
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    memset(pml4, 0, PAGE_SIZE);

    uint64_t pdp_phys = palloc_pages(&kernel_pool, 1);
    if (pdp_phys == 0) {
        return 0;
    }
    uint64_t *pdp = phys_to_virt(pdp_phys);
    memset(pdp, 0, PAGE_SIZE);
    pml4[0] = pdp_phys | 7;

    uint64_t pd0_phys = palloc_pages(&kernel_pool, 1);
    if (pd0_phys == 0) {
        return 0;
    }
    uint64_t *pd0 = phys_to_virt(pd0_phys);
    memset(pd0, 0, PAGE_SIZE);

    {
        uint64_t *loader_pd_low = phys_to_virt(0x92000);
        pd0[0] = loader_pd_low[0] & ~(uint64_t)PTE_U;
    }
    pdp[0] = pd0_phys | 7;

    pdp[1] = 0x96000 | 7;

    uint64_t pd2_phys = palloc_pages(&kernel_pool, 1);
    if (pd2_phys == 0) {
        return 0;
    }
    uint64_t *pd2 = phys_to_virt(pd2_phys);
    memset(pd2, 0, PAGE_SIZE);
    {
        uint64_t *loader_pd_lfb = phys_to_virt(0x94000);
        pd2[0] = loader_pd_lfb[0];
    }
    pdp[2] = pd2_phys | 7;

    /*
     * 3GB-4GB 内核区: 不能再用 0x87 (1GB 大页 identity -> 物理0-1GB)。
     * KASLR 开启时内核被 loader 随机加载到物理 l_kphys (如 0x5400000),
     * 启动页表用 PD@0x98000 的 PD[1] 把虚拟 0xC0200000+off 映射到
     * l_kphys+off。若用户进程页表直接 identity, 子进程一被调度
     * (process_activate -> CR3 切换) 访问内核代码就映射到错误物理地址,
     * 执行垃圾/零页, exec 卡死、wait 永不返回。
     * 因此直接把 loader 的 3GB PD@0x98000 拷贝一份作为进程的 PD。
     */
    {
        uint64_t pd3_phys = palloc_pages(&kernel_pool, 1);
        if (pd3_phys == 0) {
            return 0;
        }
        uint64_t *pd3 = phys_to_virt(pd3_phys);
        memset(pd3, 0, PAGE_SIZE);
        memcpy(pd3, phys_to_virt(0x98000), PAGE_SIZE);
        pdp[3] = pd3_phys | 7;
    }

    return (uint32_t *)(uintptr_t)pml4_phys;
}
void create_user_vaddr_bitmap(struct task_struct *user_prog) {
    user_prog->userprog_v_addr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP(
        (0xc0000000 - USER_VADDR_START) / PAGE_SIZE / 8, PAGE_SIZE);
    user_prog->userprog_v_addr.vaddr_bitmap.bits =
        (uint8_t *)get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_v_addr.vaddr_bitmap.btmp_bytes_len =
        (0xc0000000 - USER_VADDR_START) / PAGE_SIZE / 8;
    bitmap_init(&user_prog->userprog_v_addr.vaddr_bitmap);
}

void process_execute(char *path, char *name) {
    struct task_struct *thread = thread_alloc_slot(name, DEFAULT_PRIO);
    struct thread_stack *ts =
        (struct thread_stack *)(thread->kernel_stack_top -
                                sizeof(struct thread_stack));
    ts->rflags = 0x202;
    ts->r15 = (uint64_t)start_process;
    ts->r14 = (uint64_t)path;
    ts->r13 = 0;
    ts->r12 = 0;
    ts->rbx = 0;
    ts->rbp = 0;
    ts->rip = kernel_thread_entry;
    create_user_vaddr_bitmap(thread);
    thread->pgdir = (uint32_t)create_page_dir();
    thread->user_brk = 0;
    kprintf("[procexec] '%s' pid=%d pgdir=0x%x\n", path, thread->pid,
            thread->pgdir);
    thread_ready(thread);
}
