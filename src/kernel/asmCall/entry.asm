bits 64
section .text

extern kmain
extern __bss_start
extern __bss_end

global entry_start
entry_start:
        ; 保存 Multiboot2
        mov     r8d, eax                ; 0x36D76289
        mov     r9, rbx                
        mov     dx, 0x3F8 + 1         
        xor     al, al
        out     dx, al
        inc     dx                      ; 0x3F9
        out     dx, al
        inc     dx                      ; 0x3FA
        out     dx, al
        inc     dx                      ; 0x3FB
        mov     al, 0x03
        out     dx, al
        inc     dx                      ; 0x3FC
        mov     al, 0x00
        out     dx, al
        inc     dx                      ; 0x3FD
        out     dx, al
        inc     dx                      ; 0x3FE
        out     dx, al
        ; 写 "K!\n"
        mov     dx, 0x3F8
        mov     al, 'K'
        out     dx, al
        mov     al, '!'
        out     dx, al
        mov     al, 0x0A
        out     dx, al

        movabs  rdi, __bss_start
        movabs  rax, __bss_end
        sub     rax, rdi               
        mov     rcx, rax
        xor     eax, eax
        cld
        rep     stosb

        cli
        mov     edi, r8d              
        mov     esi, r9d              
        call    kmain
.spin:  hlt
        jmp     .spin