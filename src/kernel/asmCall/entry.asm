bits 32
section .text

extern KMain
extern __bss_start
extern __bss_end

global entry_start
entry_start:
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


        mov     edi, __bss_start
        mov     ecx, __bss_end
        sub     ecx, __bss_start
        xor     eax, eax
        cld
        rep     stosb

        cli
.spin:  hlt
        jmp     .spin
