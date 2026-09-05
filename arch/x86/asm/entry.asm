bits 64
section .text

extern kmain
extern __bss_start
extern __bss_end

global entry_start
entry_start:

        mov     r8d, eax
        mov     r9, rbx
        mov     r10d, edx
        mov     dx, 0x3F8 + 1         
        xor     al, al
        out     dx, al
        inc     dx
        out     dx, al
        inc     dx
        out     dx, al
        inc     dx
        mov     al, 0x03
        out     dx, al
        inc     dx
        mov     al, 0x00
        out     dx, al
        inc     dx
        out     dx, al
        inc     dx
        out     dx, al

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
        mov     edx, r10d             
        call    kmain
.spin:  hlt
        jmp     .spin