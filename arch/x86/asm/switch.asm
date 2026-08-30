bits 64
section .text

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