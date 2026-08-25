[bits 16]
org 0x9000

start16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    mov     byte [0x7000], 0x41        ; 'A'

    lgdt [_tmp_gdtr]               
    mov eax, cr0
    or  eax, 1                      ; PE
    mov cr0, eax
    jmp 0x18:start32

[bits 32]
start32:
    mov     byte [0x7001], 0x42        ; 'B'

    mov ax, 0x10                    ; 临时数据段
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7c00

    mov eax, 0x90000
    mov cr3, eax

    mov eax, cr4
    or  eax, 0x20                   ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080             ; EFER
    rdmsr
    or  eax, 0x100                  ; LME
    wrmsr

    mov eax, cr0
    or  eax, 0x80000001             ; PG | PE (使能 LMA)
    mov cr0, eax
    jmp 0x08:start64                ; 切到 64 位代码段

[bits 64]
start64:
    mov     byte [0x7002], 0x43        ; 'C'

    mov rax, 0x8000
    mov ebx, [rax + 0x00]           ; gdtr base
    mov edx, [rax + 0x04]           ; stack_top 
    mov ecx, [rax + 0x08]           ; ap_main
    mov r10d, [rax + 0x0C]          ; index

    lgdt [rbx]                     

    mov     byte [0x7003], 0x44        ; 'D'

    mov ax, 0x10                   
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov ax, 0x40                    ; percpu gs
    mov gs, ax

    mov rdi, r10                    
    mov rsp, rdx
    call rcx
.hang:
    cli
    hlt
    jmp .hang

align 8
_tmp_gdt:
    dq 0                                                ; 0x00 null
    db 0xff,0xff,0x00,0x00,0x00,0x9a,0x20,0x00          ; 0x08 64-bit code (L=1)
    db 0xff,0xff,0x00,0x00,0x00,0x92,0xcf,0x00          ; 0x10 data
    db 0xff,0xff,0x00,0x00,0x00,0x9a,0xcf,0x00          ; 0x18 32-bit compat code
_tmp_gdtr:
    dw (_tmp_gdt_end - _tmp_gdt - 1)
    dd _tmp_gdt
_tmp_gdt_end: