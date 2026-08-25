[bits 64]
section .text

; ---- 中断入口 (64 位) ----
; 每个 ISR 先压入 err_code (无错码则补 0) 和向量号, 再进公共入口。
; 公共入口保存全部 GPR 后, 把帧指针传给 C 的 isr_handler/irq_handler。

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
    ; 保存用户 gs(可能被 TLS 改成 0x38), 换内核 percpu gs=0x40。
    ; 不占用任何 GPR 暂存(避免破坏尚未压栈的 rax/rbx):
    ; gs 槽位于 rax 之上, 对应 struct Registers 的 gs_saved 字段。
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
    ; 恢复 GPR
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
    pop  gs                   ; 恢复用户 gs
    add  rsp, 16             ; 略过错码 + 向量号
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
    push gs                   ; 保存(可能为用户 TLS 的)gs
    push qword 0x40
    pop  gs                   ; 内核 percpu gs
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
    pop  gs                   ; 恢复用户 gs
    add  rsp, 16
    iretq
global intr_exit
intr_exit:
    ; 与公共入口的保存顺序匹配恢复, 然后 iretq 返回。
    ; 必须与 isr_common_stub/irq_common_stub/syscall_common_stub 一致:
    ; 先 pop gs (gs_saved 槽位), 再 add rsp,16 跳过 int_no+err_code,
    ; 否则 IRET 帧错位 8 字节导致 #GP。
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
    ; int 0x80 软中断。64 位内核下保留入口(供 32 位兼容用户程序调用),
    ; 向量与帧暂沿用异常入口, 具体 32/64 用户兼容在后续扩展。
    push qword 0
    push qword 0x80
    jmp  syscall_common_stub
extern syscall_handler
syscall_common_stub:
    ; 与 struct Registers 一致: r15@低地址 ... rax@0x70(高地址)。
    ; 必须先 push rax、最后 push r15, 使 rsp 指向 r15 槽位;
    ; 否则 r->eax 读到的是保存的 r15, 系统调用号全错, 所有 syscall 返回 -1。
    push gs                   ; 保存用户 gs(槽位在 rax 之上, 对应 gs_saved)
    push qword 0x40
    pop  gs                   ; 内核 percpu gs
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
    mov  [rsp + 14*8], rax     ; 返回值写回 rax 槽位(struct 偏移 0x70)
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
    pop  gs                   ; 恢复用户 gs
    add  rsp, 16
    iretq