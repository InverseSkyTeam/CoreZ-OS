bits 64
section .text

global asm_hlt
asm_hlt: hlt
        ret

global asm_xchg
asm_xchg:                       ; uint32_t asm_xchg(volatile uint32_t* addr, uint32_t newval)
        mov     eax, esi
        xchg    eax, [rdi]
        ret

global asm_pause
asm_pause:
        pause
        ret

global asm_cli
asm_cli: cli
        ret

global asm_sti
asm_sti: sti
        ret

global asm_stihlt
asm_stihlt: sti
            hlt
            ret

global asm_read_cr0
asm_read_cr0:                   ; uint64_t
        mov     rax, cr0
        ret

global asm_write_cr0
asm_write_cr0:                  ; void asm_write_cr0(uint64_t cr0)
        mov     cr0, rdi
        ret

global asm_read_cr2
asm_read_cr2:                   ; uint64_t
        mov     rax, cr2
        ret

global asm_read_cr4
asm_read_cr4:                   ; uint64_t
        mov     rax, cr4
        ret

global asm_write_cr4
asm_write_cr4:                  ; void asm_write_cr4(uint64_t cr4)
        mov     cr4, rdi
        ret

global asm_write_cr3
asm_write_cr3:                  ; void asm_write_cr3(uint64_t cr3)
        mov     cr3, rdi
        ret

global asm_read_cr3
asm_read_cr3:                   ; uint64_t
        mov     rax, cr3
        ret

global asm_rdmsr
asm_rdmsr:                      ; uint64_t asm_rdmsr(uint32_t msr)
        mov     ecx, edi
        rdmsr
        shl     rdx, 32
        or      rax, rdx
        ret

global asm_wrmsr
asm_wrmsr:                      ; void asm_wrmsr(uint32_t msr, uint64_t value)
        mov     ecx, edi
        mov     eax, esi
        shr     rsi, 32
        mov     edx, esi
        wrmsr
        ret

global asm_save_eflags
asm_save_eflags:                ; uint64_t (rflags)
        pushfq
        pop     rax
        ret

global asm_restore_eflags
asm_restore_eflags:             ; void asm_restore_eflags(uint64_t eflags)
        push    rdi
        popfq
        ret

global asm_lgdt
asm_lgdt:                       ; void asm_lgdt(uintptr_t gdtr_ptr)
        lgdt    [rdi]
        ret

global asm_ltr
asm_ltr:                        ; void asm_ltr(uint16_t sel)
        mov     ax, di
        ltr     ax
        ret

global asm_str
asm_str:                        ; uint16_t
        str     ax
        ret

global detect_64bit
detect_64bit:                   ; 已是 64 位, 恒返回 1
        mov     eax, 1
        ret

; 新内核线程首次被调度时的入口跳板。
; switch.asm 在恢复线程栈时会把 r15/r14 改成线程栈中保存的值 →
; 约定 r15 = 线程函数, r14 = 参数, 这里是它们的唯一使用处。
global kernel_thread_entry
kernel_thread_entry:
        mov     rdi, r15        ; thread_func function
        mov     rsi, r14        ; void *arg
        xor     ebp, ebp        ; 新线程 rbp 清零
        extern  kernel_thread_entry_c
        call    kernel_thread_entry_c
        ; 不应返回, 若返回则死循环
.endless:
        cli
        hlt
        jmp     .endless