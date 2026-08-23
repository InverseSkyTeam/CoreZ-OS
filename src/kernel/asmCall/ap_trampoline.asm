[bits 16]
org 0x9000

start16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00                 

    lgdt [_tmp_gdtr16]            

    mov eax, cr0
    or  eax, 1
    mov cr0, eax                  
    jmp 0x10:start32              

    [bits 32]
start32:
    mov ax, 0x18                  
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov esp, 0x7c00               

    mov eax, 0x400000              
    mov cr3, eax
    mov eax, cr4
    or  eax, 0x10                 
    mov cr4, eax
    mov eax, cr0
    or  eax, 0x80000000          
    mov cr0, eax
    jmp _pg_on
_pg_on:
    mov eax, [0x8000]               
    lgdt [eax]
    mov ebx, [0x8004]              
    mov esp, ebx
    mov ax, 0x38                  
    mov gs, ax
    push dword [0x800C]            
    mov eax, [0x8008]              
    call eax
    cli
.hang:
    hlt
    jmp .hang

    align 8
_tmp_gdt:
    dd 0, 0
    ; data
    db 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00
    ; code
    db 0xff, 0xff, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
    ; data
    db 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00
_tmp_gdtr16:
    dw (_tmp_gdt_end - _tmp_gdt - 1) & 0xFFFF
    dd _tmp_gdt
_tmp_gdtr32:
    dw (_tmp_gdt_end - _tmp_gdt - 1) & 0xFFFF
    dd _tmp_gdt
_tmp_gdt_end: