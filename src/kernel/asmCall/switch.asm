bits 64
section .text

; void switch_to(uintptr_t* cur_kstack, uintptr_t* next_kstack)
; 保存当前上下文到 [cur], 从 [next] 恢复。
global switch_to
switch_to:
        push    rbp
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        pushfq
        mov     [rdi], rsp
        mov     rsp, [rsi]
        popfq
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        pop     rbp
        ret