bits 64
section .text

extern KMain
extern __bss_start
extern __bss_end

global entry_start
entry_start:
        ;
        ; 由 loader.asm 通过 "jmp 0x08:KERNEL_VIRT" 进入, EIP = 0xC0280000。
        ; 此时 CPU 处于真正的 64 位长模式:
        ;   - CR0.PG = 1, CR4.PAE = 1, EFER.LME = 1
        ;   - CR3    = 0x90000 (loader 设置的 PML4)
        ;   - CS     = 0x08 (64 位代码段, L=1)
        ;   - DS/ES/FS/GS/SS = 0x10 (64 位平坦数据段)
        ;   - RSP    = 0x90000 (loader 设置)
        ;
        ; loader 已把页表做好(恒等映射 0~12MB + 高半 0xC0000000~0xFFFFFFFF
        ; 1GB 大页), 这里不再重建页表, 直接清 BSS 后调用 KMain。
        ; 采用 System V AMD64 调用约定 (KMain 无参数)。
        ;

        ; 串口 (0x3F8) 显式诊断, 确认 KMain 真的跑进来了
        mov     dx, 0x3F8 + 1           ; 屏蔽中断
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

        ; 清零 BSS 段 (虚拟地址 0xC0xxxxxx, 由高半 1GB 大页映射到物理低端)
        movabs  rdi, __bss_start
        movabs  rax, __bss_end
        sub     rax, rdi                ; 长度 = __bss_end - __bss_start
        mov     rcx, rax
        xor     eax, eax
        cld
        rep     stosb

        ; KMain 是 64 位 C 函数, 无参数, System V ABI。
        ; 进入下面调用时 RSP 保持 16 字节对齐 (loader 设 RSP=0x90000)。
        call    KMain

        ; KMain 正常不会返回 (内核主循环 hlt)。
        ; 万一返回, 进入停机循环。
        cli
.spin:  hlt
        jmp     .spin