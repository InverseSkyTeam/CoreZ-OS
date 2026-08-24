bits 32
section .text

global asm_hlt
asm_hlt: hlt
        ret

global asm_xchg
asm_xchg:
        mov     eax, [esp+4]    
        mov     ecx, [esp+8]    
        xchg    [eax], ecx      
        mov     eax, ecx
        ret

global asm_pause
asm_pause:
        pause
        ret

global asm_cli
asm_cli: cli
        ret

global asm_sti
asm_sti: sti
        ret

global asm_stihlt
asm_stihlt: sti
            hlt
            ret

global asm_read_cr0
asm_read_cr0:
        mov     eax, cr0
        ret

global asm_write_cr0
asm_write_cr0:
        mov     eax, [esp+4]
        mov     cr0, eax
        jmp     .flush
.flush:
        ret

global asm_read_cr4
asm_read_cr4:
        mov     eax, cr4
        ret

global asm_write_cr4
asm_write_cr4:
        mov     eax, [esp+4]
        mov     cr4, eax
        ret

global asm_rdmsr
asm_rdmsr:
        mov     ecx, [esp+4]
        rdmsr
        ret

global asm_wrmsr
asm_wrmsr:
        mov     ecx, [esp+4]
        mov     eax, [esp+8]
        mov     edx, [esp+12]
        wrmsr
        ret

global asm_write_cr3
asm_write_cr3:
        mov     eax, [esp+4]
        mov     cr3, eax
        ret

global asm_save_eflags
asm_save_eflags:
        pushfd
        pop     eax
        ret

global asm_restore_eflags
asm_restore_eflags:
        mov     eax, [esp+4]
        push    eax
        popfd
        ret

global asm_lgdt
asm_lgdt:
        mov     eax, [esp+4]
        lgdt    [eax]
        ret

global asm_ltr
asm_ltr:
        mov     ax, [esp+4]
        ltr     ax
        ret

global asm_str
asm_str:
        str     eax
        ret

global detect_64bit
detect_64bit:
        pushfd

        pop     eax
        mov     ecx, eax
        btc     eax, 21
        push    eax
        popfd
        pushfd
        pop     eax
        cmp     eax, ecx
        je      .no_cpuid

        mov     eax, 0x80000000
        cpuid 
        cmp     eax, 0x80000001
        jb      .no_long_mode

        mov     eax, 0x80000001
        cpuid 
        bt      edx, 29
        jc      .long_mode_supported

.no_long_mode:
        mov     eax, 0
        ret 

.long_mode_supported:
        mov     eax, 1
        ret

.no_cpuid:
        mov     eax, 0
        ret