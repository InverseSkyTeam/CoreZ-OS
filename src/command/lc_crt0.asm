; 参考: Wine ntdll 的进程入口 (ldt 初始化) + Linux i386 ABI 的 _start (crt1.o) 栈解析
[bits 32]
extern main
extern __lc_tls_init
extern __lc_terminate

section .text

global _lc_start
_lc_start:
    mov     ecx, [esp]              ; ecx = argc
    mov     esi, ecx                ; esi = argc (callee-saved)
    lea     edx, [esp + 4]          ; edx = argv
    mov     edi, edx                ; edi = argv
    lea     ebx, [edx + ecx*4 + 4]  ; ebx = envp (argv + argc*4 + NULL)

    ; 由 envp 向后扫描空串, 定位 auxv 数组
    mov     ecx, ebx
.lc_auxv_scan:
    cmp     dword [ecx], 0
    je      .lc_auxv_found
    add     ecx, 4
    jmp     .lc_auxv_scan
.lc_auxv_found:
    add     ecx, 4                  ; ecx = auxv 起始
    mov     [__lc_auxv], ecx

    call    __lc_tls_init

    push    ebx                     ; 3rd: envp
    push    edi                     ; 2nd: argv
    push    esi                     ; 1st: argc
    call    main
    add     esp, 12

    push    eax
    call    __lc_terminate
.lc_halt:
    jmp     .lc_halt

; __lc_syscall6: 七参 cdecl, 全部经寄存器送 goto int 0x80 (six-register ABI)
; 入栈(从低到高): retaddr, nr, a, b, c, d, e, f
global __lc_syscall6
__lc_syscall6:
    push    ebp
    mov     eax, [esp + 8]
    mov     ebx, [esp + 12]
    mov     ecx, [esp + 16]
    mov     edx, [esp + 20]
    mov     esi, [esp + 24]
    mov     edi, [esp + 28]
    mov     ebp, [esp + 32]
    int     0x80
    pop     ebp
    ret

section .data
global __lc_auxv
__lc_auxv: dd 0