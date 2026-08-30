[bits 64]
section .text

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common_stub
%endmacro
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
extern isr_handler
isr_common_stub:
    push gs
    push qword 0x40
    pop  gs
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov  rdi, rsp
    call isr_handler

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    pop  gs                  
    add  rsp, 16             
    iretq
%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
extern irq_handler
irq_common_stub:
    push gs                
    push qword 0x40
    pop  gs                   
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov  rdi, rsp
    call irq_handler
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    pop  gs                  
    add  rsp, 16
    iretq
global intr_exit
intr_exit:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    pop  gs
    add  rsp, 16
    iretq
global default_handler
default_handler:
    push qword 0
    push qword 0xFFFF
    jmp  isr_common_stub
global syscall_0x80
syscall_0x80:
    push qword 0
    push qword 0x80
    jmp  syscall_common_stub
extern syscall_handler
syscall_common_stub:
    push gs                  
    push qword 0x40
    pop  gs                 
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov  rdi, rsp
    call syscall_handler
    mov  [rsp + 14*8], rax   
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    pop  gs                 
    add  rsp, 16
    iretq

global syscall_entry
syscall_entry:
    mov r15, rsp

    xchg rcx, r11

    push rax        ; [rsp+0]  syscall number
    push rbx        ; [rsp+8]
    push rcx        ; [rsp+16] user RIP (for sysretq)
    push rdx        ; [rsp+24] arg3
    push rsi        ; [rsp+32] arg2
    push rdi        ; [rsp+40] arg1
    push rbp        ; [rsp+48]
    push r8         ; [rsp+56] arg5
    push r9         ; [rsp+64] arg6
    push r10        ; [rsp+72] arg4
    push r11        ; [rsp+80] user RFLAGS (for sysretq)
    push r12        ; [rsp+88]
    push r13        ; [rsp+96]
    push r14        ; [rsp+104]
    push r15        ; [rsp+112] user RSP

    push gs
    push qword 0x40
    pop gs

    sub rsp, 72

    mov qword [rsp + 72], 0x81

    mov qword [rsp + 80], 0

    mov rax, [rsp + 88]
    mov [rsp + 96], rax

    mov qword [rsp + 104], 0x33

    mov rax, [rsp + 152]
    mov [rsp + 112], rax

    mov rax, [rsp + 184]
    mov [rsp + 120], rax

    mov qword [rsp + 128], 0x23

    mov rdi, rsp
    call syscall_handler

    add rsp, 72
    add rsp, 16
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11        ; user RFLAGS (for sysretq)
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx        ; user RIP (for sysretq)
    pop rbx
    pop rax

    mov rsp, r15
    sysretq
