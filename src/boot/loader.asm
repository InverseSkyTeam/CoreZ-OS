KERNEL  equ     0x00280000
DSKCAC  equ     0x00100000
DSKCAC0 equ     0x00008000
VBEMODE equ     0x105
VBEINFO equ     0x0FF0 - 256

CYLS    equ     0x0FF0
LEDS    equ     0x0FF1
VMODE   equ     0x0FF2
SCRNX   equ     0x0FF4
SCRNY   equ     0x0FF6
VRAM    equ     0x0FF8
VRAMBYTES equ  0x0FFC

STACK_PHYS equ  0x00090000

%define PART_LBA        2048
%define RESERVED_SECT   1
%define FAT_SECTORS     256
%define ROOT_SECTORS    32
%define SPC             1
FAT_LBA   equ PART_LBA + RESERVED_SECT
ROOT_LBA  equ FAT_LBA + 2*FAT_SECTORS
DATA_LBA  equ ROOT_LBA + ROOT_SECTORS
DIR_BUF   equ 0x3000
FAT_BUF   equ 0x3400

        org     0xC200

kernel_addr:    dd      0x00010000

        mov     al, byte [0x0FFE]
        mov     byte [l_drive], al

        mov     ax, 0x9000
        mov     es, ax
        mov     di, 0
        mov     cx, VBEMODE
        mov     ax, 0x4F01
        int     0x10
        cmp     ax, 0x004F
        jne     vbe_fail

        mov     bx, VBEMODE + 0x4000
        mov     ax, 0x4F02
        int     0x10
        cmp     ax, 0x004F
        jne     vbe_fail

        mov     byte [VMODE], 8
        mov     ax, [es:0x0012]
        mov     [SCRNX], ax
        mov     ax, [es:0x0014]
        mov     [SCRNY], ax
        mov     eax, [es:0x0028]
        mov     [VRAM], eax
        
        xor     eax, eax
        mov     ax, [es:0x0010]
        movzx   ecx, word [es:0x0014]
        mul     ecx
        mov     [VRAMBYTES], eax
        jmp     vbe_done

vbe_fail:
        mov     al, 0x13
        mov     ah, 0x00
        int     0x10
        mov     byte [VMODE], 8
        mov     word [SCRNX], 320
        mov     word [SCRNY], 200
        mov     dword [VRAM], 0x000A0000
        mov     dword [VRAMBYTES], 64000   

vbe_done:
        mov     ax, 0x1130
        mov     bh, 0x06
        int     0x10

        push    ds
        push    es
        push    si
        push    di
        push    cx

        mov     ax, es
        mov     si, bp

        mov     bx, 0x9C00
        mov     es, bx
        xor     di, di

        mov     ds, ax

        mov     cx, 256 * 16
        rep     movsb

        pop     cx
        pop     di
        pop     si
        pop     es
        pop     ds

        mov     ah, 0x02
        int     0x16
        mov     [LEDS], al

        mov     dword [0x6000], 0
        mov     edi, 0x6004

.e820_loop:
        mov     eax, 0xE820
        mov     edx, 0x534D4150
        mov     ecx, 24
        int     0x15
        jc      .e820_done
        cmp     eax, 0x534D4150
        jne     .e820_done
        add     edi, 24
        inc     dword [0x6000]
        cmp     ebx, 0
        je      .e820_done
        jmp     .e820_loop

.e820_done:
        mov     al, 0xFF
        out     0x21, al
        nop
        out     0xA1, al

        mov     dword [l_loadcur], 0x00010000

        xor     ax, ax
        mov     es, ax
        mov     word [l_dirsec], 0
.l_dirscan:
        mov     ax, word [l_dirsec]
        add     ax, ROOT_LBA
        mov     word [l_buf], DIR_BUF
        call    l_readsec
        jc      .lkerr
        mov     bx, DIR_BUF
        mov     di, 16
.l_entry:
        cmp     byte [bx], 0x00
        je      .lkerr
        cmp     byte [bx], 0xE5
        je      .l_enext
        push    si
        push    bx
        push    di
        mov     si, l_kname
        mov     di, bx
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     bx
        pop     si
        je      .l_found
.l_enext:
        add     bx, 32
        dec     di
        jnz     .l_entry
        inc     word [l_dirsec]
        cmp     word [l_dirsec], ROOT_SECTORS
        jb      .l_dirscan
        jmp     .lkerr

.l_found:
        mov     ax, word [bx+26]
        mov     word [l_cluster], ax

.l_loadloop:
        mov     ax, word [l_cluster]
        sub     ax, 2
        mov     dx, SPC
        mul     dx
        add     ax, DATA_LBA
        mov     word [l_lba], ax
        mov     eax, dword [l_loadcur]
        mov     ecx, eax
        and     ecx, 0x0F
        mov     word [l_off], cx
        shr     eax, 4
        mov     word [l_seg], ax
        mov     word [dap+0x02], SPC
        mov     ax, word [l_off]
        mov     word [dap+0x04], ax
        mov     ax, word [l_seg]
        mov     word [dap+0x06], ax
        mov     ax, word [l_lba]
        mov     word [dap+0x08], ax
        mov     word [dap+0x0C], 0
        mov     si, dap
        mov     dl, byte [l_drive]
        mov     ah, 0x42
        int     0x13
        jc      .lkerr
        mov     ax, SPC
        shl     ax, 9
        movzx   eax, ax
        add     dword [l_loadcur], eax

        mov     ax, word [l_cluster]
        shl     ax, 1
        mov     word [l_fbyte], ax
        mov     ax, word [l_fbyte]
        shr     ax, 9
        add     ax, FAT_LBA
        mov     word [l_buf], FAT_BUF
        call    l_readsec
        jc      .lkerr
        mov     ax, word [l_fbyte]
        and     ax, 0x1FF
        mov     di, ax
        mov     ax, word [FAT_BUF + di]
        mov     word [l_cluster], ax
        cmp     ax, 0xFFF8
        jae     .l_kload_ok
        jmp     .l_loadloop

.l_kload_ok:
        jmp     .kload_ok

.lkerr:
        mov     si, kmsg
.kerr:
        lodsb
        test    al, al
        jz      .khalt
        mov     ah, 0x0E
        mov     bx, 15
        int     0x10
        jmp     .kerr
.khalt:
        hlt
        jmp     .khalt
.kload_ok:

        cli

        call    waitkbdout
        mov     al, 0xD1
        out     0x64, al
        call    waitkbdout
        mov     al, 0xDF
        out     0x60, al
        call    waitkbdout

        lgdt    [GDTR0]
        lidt    [IDTR0]
        mov     eax, cr0
        and     eax, 0x7FFFFFFF
        or      eax, 0x00000001
        mov     cr0, eax
        jmp     pipelineflush

pipelineflush:
        mov     ax, 1*8
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax

        mov     eax, cr4
        or      eax, 0x600
        mov     cr4, eax

        mov     esi, [kernel_addr]
        mov     edi, KERNEL
        mov     ecx, 512*1024/4
        call    memcpy

        mov     esp, STACK_PHYS

        jmp     DWORD 2*8:KERNEL

waitkbdout:
        in      al, 0x64
        and     al, 0x02
        jnz     waitkbdout
        ret

memcpy:
        mov     eax, [esi]
        add     esi, 4
        mov     [edi], eax
        add     edi, 4
        sub     ecx, 1
        jnz     memcpy
        ret

        alignb  16

        align   8
dap:
        db      0x10
        db      0
        dw      0
        dw      0
        dw      0
        dd      0
        dd      0

l_drive: db     0
l_loadcur: dd   0
l_dirsec: dw    0
l_cluster: dw   0
l_fbyte:  dw    0
l_buf:    dw    0
l_lba:    dw    0
l_off:    dw    0
l_seg:    dw    0
l_kname:  db    "KERNEL  BIN"

l_readsec:
        push    si
        mov     word [dap+0x02], 1
        mov     si, ax
        mov     ax, word [l_buf]
        mov     word [dap+0x04], ax
        mov     word [dap+0x06], 0
        mov     word [dap+0x08], si
        mov     word [dap+0x0C], 0
        mov     si, dap
        mov     dl, byte [l_drive]
        mov     ah, 0x42
        int     0x13
        pop     si
        ret

kmsg:   db      0x0A, 0x0A, "kernel load error", 0

IDT0:
    %rep 256
        dw  default_handler
        dw  0x08
        db  0
        db  0x8E
        dw  0
    %endrep

IDTR0:
    dw  256*8 - 1
    dd  IDT0

default_handler:
    iret

GDT0:
    db 0, 0, 0, 0, 0, 0, 0, 0
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF

        dw      0
GDTR0:
        dw      8*3-1
        dd      GDT0

        alignb  16
