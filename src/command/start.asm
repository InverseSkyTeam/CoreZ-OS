[bits 32]
extern main
extern exit

section .text
global _start
_start:
    mov     ecx, [esp]          ; ecx = argc
    lea     edx, [esp + 4]      ; edx = argv
    lea     ebx, [edx + ecx*4 + 4]  ; ebx = envp (after argv NULL)
    push    ebx                 ; 3rd arg: envp
    push    edx                 ; 2nd arg: argv
    push    ecx                 ; 1st arg: argc
    call    main

    push    eax
    call    exit