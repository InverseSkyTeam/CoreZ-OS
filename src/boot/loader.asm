KERNEL  equ     0x00280000
KERNEL_VIRT equ 0xC0000000 + KERNEL
KASLR_MIN  equ    0x00800000
KASLR_SLOTS equ   124
KERNEL_FIXED equ  0x00280000

MENU_ITEMS   equ  2
MENU_TIMEOUT equ  5
TICKS_PER_SEC equ 18     
KEY_UP       equ  0x48
KEY_DOWN     equ  0x50
KEY_ENTER    equ  0x1C
KEY_1        equ  0x02
KEY_2        equ  0x03
FONT_BASE    equ  0x9C000   
MENU_BG      equ  0x00
MENU_NORMAL  equ  0x07
MENU_HI      equ  0x0E
MENU_TITLE   equ  0x0F
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
MBI        equ  0x00005000   ; Multiboot2 物理地址

%define PART_LBA        2048
%define RESERVED_SECT   2
%define FAT_SECTORS     127
%define FAT_COUNT       2
%define SPC             1
%define ROOT_CLUSTER    2
FAT_LBA   equ PART_LBA + RESERVED_SECT
DATA_LBA  equ FAT_LBA + FAT_COUNT*FAT_SECTORS
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
        call    set_palette
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
        mov     word [l_rowcl], ROOT_CLUSTER
.l_dirscan:
        mov     ax, word [l_rowcl]
        sub     ax, 2
        add     ax, DATA_LBA
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
        shl     ax, 2
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
        mov     eax, dword [FAT_BUF + di]
        and     eax, 0x0FFFFFFF
        mov     word [l_cluster], ax
        cmp     eax, 0x0FFFFFF8
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
        jmp     DWORD 2*8:pipelineflush

pipelineflush:
        bits    32

        mov     ax, 1*8
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax

        call    boot_menu

        call    pick_kphys
        mov     eax, [l_kphys]
        add     eax, KERNEL - 0x200000
        mov     edi, eax
        mov     esi, [kernel_addr]
        mov     ecx, 512*1024/4
        call    memcpy

        mov     esp, STACK_PHYS

        mov     edi, 0x90000
        mov     ecx, 0x9000 / 4
        xor     eax, eax
        rep     stosd

        mov     dword [0x90000],     0x91007
        mov     dword [0x91000],     0x92007
        call    setup_hpd
        mov     dword [0x92000 + 0*8], 0x000083
        mov     dword [0x92000 + 1*8], 0x200083
        mov     dword [0x92000 + 2*8], 0x400083
        mov     dword [0x92000 + 3*8], 0x600083
        mov     dword [0x92000 + 4*8], 0x800083
        mov     dword [0x92000 + 5*8], 0xA00083

        mov     dword [0x91000 + 2*8],  0x94007     ; PDPT[2] -> PD @0x94000
        mov     dword [0x94000 + 0*8],  0xfd000083  ; VA 0x80000000 -> phys 0xfd000000 (LFB)

        mov     dword [0x91000 + 1*8],  0x96007     ; PDPT[1] -> PD @0x96000
        mov     dword [0x96000 + 0*8],  0xfec00083  ; VA 0x40000000 -> phys 0xfec00000 (IOAPIC)
        mov     dword [0x96000 + 1*8],  0xfee00083  ; VA 0x40200000 -> phys 0xfee00000 (LAPIC)

        mov     edi, MBI
        mov     dword [edi+0], 0       
        mov     dword [edi+4], 0        ; reserved
        add     edi, 8

        mov     dword [edi+0], 8        ; type
        mov     dword [edi+4], 40       ; size
        mov     eax, [VRAM]
        mov     dword [edi+8],  eax     ; framebuffer_addr 低32
        mov     dword [edi+12], 0       ; framebuffer_addr 高32
        movzx   eax, word [SCRNX]
        mov     dword [edi+16], eax     ; framebuffer_pitch
        movzx   eax, word [SCRNX]
        mov     dword [edi+20], eax     ; framebuffer_width
        movzx   eax, word [SCRNY]
        mov     dword [edi+24], eax     ; framebuffer_height
        mov     byte  [edi+28], 8       ; framebuffer_bpp
        mov     byte  [edi+29], 1       ; framebuffer_type = direct RGB
        mov     byte  [edi+30], 0       ; reserved
        mov     byte  [edi+31], 8       ; red_mask_size
        mov     byte  [edi+32], 16      ; red_field_position
        mov     byte  [edi+33], 8       ; green_mask_size
        mov     byte  [edi+34], 8       ; green_field_position
        mov     byte  [edi+35], 8       ; blue_mask_size
        mov     byte  [edi+36], 0       ; blue_field_position
        add     edi, 40

        mov     dword [edi+0], 0
        mov     dword [edi+4], 8
        add     edi, 8

        mov     eax, edi
        sub     eax, MBI
        mov     dword [MBI], eax

        lgdt    [GDTR64]

        mov     eax, cr4
        or      eax, 0x20
        mov     cr4, eax

        mov     eax, 0x90000
        mov     cr3, eax

        mov     ecx, 0xC0000080
        rdmsr
        or      eax, 0x100
        wrmsr

        mov     eax, cr0
        or      eax, 0x80000000
        mov     cr0, eax

        jmp     0x08:lg64

        bits    64
lg64:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     rsp, STACK_PHYS

        mov     eax, 0x36D76289
        mov     ebx, MBI
        mov     rcx, KERNEL_VIRT
        mov     edx, [l_kphys]
        jmp     rcx

        bits    16
waitkbdout:
        in      al, 0x64
        and     al, 0x02
        jnz     waitkbdout
        ret

bits    32
memcpy:
        mov     eax, [esi]
        add     esi, 4
        mov     [edi], eax
        add     edi, 4
        sub     ecx, 1
        jnz     memcpy
        ret

pick_kphys:
        cmp     dword [l_kaslr], 0
        jne     .do_kaslr
        mov     eax, KERNEL_FIXED
        mov     [l_kphys], eax
        ret
.do_kaslr:
        rdtsc
        mov     ebx, eax
        rdtsc
        xor     ebx, eax
        mov     eax, [0x6000]
        xor     ebx, eax
        mov     al, 0
        out     0x70, al
        in      al, 0x71
        movzx   eax, al
        xor     ebx, eax
        imul    ebx, ebx, 1103515245
        add     ebx, 12345
        mov     eax, ebx
        xor     edx, edx
        mov     ecx, KASLR_SLOTS
        div     ecx
        mov     eax, edx
        shl     eax, 21
        add     eax, KASLR_MIN
        mov     [l_kphys], eax
        ret

setup_hpd:
        mov     edi, 0x98000
        mov     ecx, 512
        xor     eax, eax
.h:
        mov     edx, eax
        shl     edx, 21
        or      edx, 0x83
        mov     [edi], edx
        mov     dword [edi+4], 0
        add     edi, 8
        inc     eax
        dec     ecx
        jnz     .h
        mov     eax, [l_kphys]
        or      eax, 0x83
        mov     dword [0x98000 + 1*8], eax
        mov     dword [0x98000 + 1*8 + 4], 0
        mov     dword [0x91000 + 3*8], 0x98007
        ret

boot_menu:
        pushad
        call    kbd_flush            
        mov     dword [menu_sel], 0
        mov     dword [menu_secs], MENU_TIMEOUT
        call    menu_clear
        call    menu_draw
        call    get_rtc_sec
        mov     [menu_last_sec], eax
.mloop:
        call    kbd_poll
        test    eax, eax
        jz      .rtc
        mov     dword [menu_secs], 0x7FFFFFFF  
        cmp     eax, KEY_UP
        je      .up
        cmp     eax, KEY_DOWN
        je      .down
        cmp     eax, KEY_1
        je      .num0
        cmp     eax, KEY_2
        je      .num1
        cmp     eax, KEY_ENTER
        je      .done
        jmp     .rtc
.up:
        dec     dword [menu_sel]
        jns     .upcheck
        mov     dword [menu_sel], MENU_ITEMS-1
        jmp     .redraw
.upcheck:
        mov     eax, [menu_sel]
        cmp     eax, MENU_ITEMS
        jb      .redraw
        mov     dword [menu_sel], MENU_ITEMS-1
        jmp     .redraw
.down:
        inc     dword [menu_sel]
        mov     eax, [menu_sel]
        cmp     eax, MENU_ITEMS
        jb      .redraw
        mov     dword [menu_sel], 0
        jmp     .redraw
.num0:
        mov     dword [menu_sel], 0
        jmp     .done
.num1:
        mov     dword [menu_sel], 1
        jmp     .done
.redraw:
        call    menu_draw
        call    menu_draw_count
        jmp     .mloop
.rtc:
        cmp     dword [menu_secs], MENU_TIMEOUT
        ja      .mloop
        call    get_rtc_sec
        cmp     eax, [menu_last_sec]
        je      .mloop
        mov     [menu_last_sec], eax
        dec     dword [menu_secs]
        cmp     dword [menu_secs], 0
        jle     .done
        call    menu_draw_count
        jmp     .mloop
.done:
        cmp     dword [menu_sel], 1
        je      sys_reboot
        mov     dword [l_kaslr], 1       
        popad
        ret

sys_reboot:
.again:
        in      al, 0x64
        test    al, 0x02
        jnz     .again
        mov     al, 0xFE
        out     0x64, al
.h:
        hlt
        jmp     .h

menu_clear:
        pushad
        mov     eax, 0
        mov     ebx, 0
        movzx   ecx, word [SCRNX]
        movzx   edx, word [SCRNY]
        mov     esi, MENU_BG
        call    fill_rect
        popad
        ret

menu_draw:
        pushad
        mov     eax, 40
        mov     ebx, 30
        mov     esi, msg_title
        mov     edx, MENU_TITLE
        call    draw_str
        mov     dword [item_idx], 0
.iloop:
        mov     eax, [item_idx]
        cmp     eax, MENU_ITEMS
        jae     .done
        imul    ecx, eax, 24
        add     ecx, 90
        mov     ebx, ecx
        mov     eax, 40
        mov     ecx, [item_idx]
        cmp     ecx, [menu_sel]
        jne     .m0
        mov     esi, msg_sel
        mov     edx, MENU_HI
        jmp     .m1
.m0:
        mov     esi, msg_unsel
        mov     edx, MENU_NORMAL
.m1:
        call    draw_str
        mov     ecx, [item_idx]
        mov     esi, [menu_items + ecx*4]
        mov     eax, 64
        mov     ecx, [item_idx]
        cmp     ecx, [menu_sel]
        jne     .l0
        mov     edx, MENU_HI
        jmp     .l1
.l0:
        mov     edx, MENU_NORMAL
.l1:
        call    draw_str
        inc     dword [item_idx]
        jmp     .iloop
.done:
        popad
        ret

menu_draw_count:
        pushad
        cmp     dword [menu_secs], MENU_TIMEOUT
        ja      .done
        movzx   eax, word [SCRNY]
        sub     eax, 28
        mov     [cnt_y], eax
        mov     eax, 40
        mov     ebx, [cnt_y]
        mov     ecx, 560
        mov     edx, 16
        mov     esi, MENU_BG
        call    fill_rect
        mov     eax, 40
        mov     ebx, [cnt_y]
        mov     esi, msg_count1
        mov     edx, MENU_NORMAL
        call    draw_str
        mov     eax, [menu_secs]
        add     eax, '0'
        mov     ecx, eax
        mov     eax, 144
        mov     ebx, [cnt_y]
        mov     edx, MENU_HI
        call    draw_char
        mov     eax, 152
        mov     ebx, [cnt_y]
        mov     esi, msg_count2
        mov     edx, MENU_NORMAL
        call    draw_str
.done:
        popad
        ret

draw_str:
        pushad
        mov     [cur_x], eax
        mov     [cur_y], ebx
.loop:
        lodsb
        test    al, al
        jz      .done
        mov     ecx, eax
        mov     eax, [cur_x]
        mov     ebx, [cur_y]
        call    draw_char
        add     dword [cur_x], 8
        jmp     .loop
.done:
        popad
        ret

draw_char:
        pushad
        mov     [tmp_x], eax
        mov     [tmp_color], edx
        mov     esi, FONT_BASE
        imul    ecx, ecx, 16
        add     esi, ecx
        movzx   ecx, word [SCRNX]
        mov     [pitch], ecx
        mov     eax, ebx
        mul     ecx
        add     eax, [tmp_x]
        mov     edi, [VRAM]
        add     edi, eax
        mov     eax, [tmp_color]
        mov     edx, 16
.row:
        movzx   ebx, byte [esi]
        push    ecx
        mov     ecx, 8
.col:
        shl     bl, 1
        jnc     .nopix
        mov     [edi], al
.nopix:
        inc     edi
        dec     ecx
        jnz     .col
        pop     ecx
        inc     esi
        push    eax
        mov     eax, [pitch]
        sub     eax, 8
        add     edi, eax
        pop     eax
        dec     edx
        jnz     .row
        popad
        ret

fill_rect:
        pushad
        mov     [tmp_x], eax
        mov     [tmp_y], ebx
        mov     [tmp_w], ecx
        mov     [tmp_h], edx
        mov     [tmp_color], esi
        movzx   ecx, word [SCRNX]
        mov     [pitch], ecx
        mov     eax, [tmp_y]
        mul     ecx
        add     eax, [tmp_x]
        mov     edi, [VRAM]
        add     edi, eax
        mov     al, [tmp_color]
        mov     edx, [tmp_h]
.row:
        push    edi
        mov     ecx, [tmp_w]
        rep     stosb
        pop     edi
        add     edi, [pitch]
        dec     edx
        jnz     .row
        popad
        ret

kbd_poll:
        in      al, 0x64
        test    al, 0x01
        jz      .none
        in      al, 0x60
        test    al, 0x80
        jnz     .none
        cmp     al, 0xE0
        je      .ext
        movzx   eax, al
        ret
.ext:
        call    kbd_waitdata
        in      al, 0x60
        test    al, 0x80
        jnz     .none
        movzx   eax, al
        ret
.none:
        xor     eax, eax
        ret

kbd_waitdata:
.wait:
        in      al, 0x64
        test    al, 0x01
        jz      .wait
        ret

kbd_flush:
        push    eax
        push    ecx
        mov     ecx, 64
.loop:
        in      al, 0x64
        test    al, 0x01
        jz      .done
        in      al, 0x60
        dec     ecx
        jnz     .loop
.done:
        pop     ecx
        pop     eax
        ret

get_rtc_sec:
.again:
        mov     al, 0x0A
        out     0x70, al
        in      al, 0x71
        test    al, 0x80
        jnz     .again
        mov     al, 0x00
        out     0x70, al
        in      al, 0x71
        mov     ecx, eax
        and     ecx, 0x0F
        shr     eax, 4
        imul    eax, eax, 10
        add     eax, ecx
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
l_kphys: dd     0
l_loadcur: dd   0
l_rowcl:  dw    0   
l_cluster: dw   0
l_fbyte:  dw    0
l_buf:    dw    0
l_lba:    dw    0
l_off:    dw    0
l_seg:    dw    0
l_kname:  db    "KERNEL  BIN"

l_kaslr:      dd    1
menu_sel:     dd    0
menu_secs:    dd    0
menu_last_sec: dd  0
item_idx:     dd    0
cnt_y:        dd    0
cur_x:        dd    0
cur_y:        dd    0
pitch:        dd    0
tmp_x:        dd    0
tmp_y:        dd    0
tmp_w:        dd    0
tmp_h:        dd    0
tmp_color:    dd    0

msg_title:  db "NiTian OS Boot Menu", 0
msg_sel:    db "> ", 0
msg_unsel:  db "  ", 0
msg_count1: db "Auto boot in ", 0
msg_count2: db "s", 0
menu_items:
        dd  msg_opt0
        dd  msg_opt2
msg_opt0: db "Boot NiTian OS", 0
msg_opt2: db "Reboot", 0

bits    16
set_palette:
        pusha
        mov     dx, 0x3C8
        mov     al, 0
        out     dx, al
        mov     si, palette
        mov     cx, 48
        mov     dx, 0x3C9
.l:
        mov     al, [si]
        out     dx, al
        inc     si
        dec     cx
        jnz     .l
        popa
        ret

palette:
        db 0,0,0
        db 0,0,42
        db 0,42,0
        db 0,42,42
        db 42,0,0
        db 42,0,42
        db 42,21,0
        db 42,42,42
        db 21,21,21
        db 21,21,63
        db 21,63,21
        db 21,63,63
        db 63,21,21
        db 63,21,63
        db 63,63,21
        db 63,63,63

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

        align   8
GDT64:
    dq 0x0000000000000000       
    dq 0x00209A0000000000      
    dq 0x00CF92000000FFFF     
    dq 0x00CF9A000000FFFF       
GDT64_LEN equ $ - GDT64

GDTR64:
    dw GDT64_LEN - 1
    dd GDT64

        alignb  16
