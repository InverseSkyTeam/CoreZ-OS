; vbr.asm — P1 FAT16 启动分区的卷引导扇区 (VBR)
; 由 MBR (boot.bin) 用 LBA 读入 0x0600 并 jmp 到此。
; 职责: 从 FAT16 根目录按文件名 "LOADER  BIN" 找到 LOADER.BIN,
;       沿 FAT 链把文件读到内存 0xC200, 然后 jmp 0x0000:0xC200。
;
; 与 make_fat.py / make_ext2.py 保持一致的分区布局:
;   P1 起始 LBA = 2048
;   reserved    = 1 扇区 (本 VBR)
;   FAT 每份 256 扇区, 共 2 份
;   根目录 32 扇区 (512 项)
;   数据区起始 = 2048 + 1 + 2*256 + 32 = 2593 ; 簇 2 对应该 LBA
;   每簇 1 扇区 (SPC=1)

ORG     0x0600

%define PART_LBA        2048
%define RESERVED_SECT   1
%define FAT_SECTORS     256
%define ROOT_SECTORS    32
%define SPC             1

FAT_LBA   equ PART_LBA + RESERVED_SECT
ROOT_LBA  equ FAT_LBA + 2*FAT_SECTORS
DATA_LBA  equ ROOT_LBA + ROOT_SECTORS
LOAD_ADDR equ 0xC200

DIR_BUF equ 0x0800
FAT_BUF equ 0x0A00

        jmp     short start
        nop
        db      "NITIAN  "
        dw      512
        db      1
        dw      RESERVED_SECT
        db      2
        dw      512
        dw      0
        db      0xF8
        dw      0
        dw      18
        dw      2
        dd      0
        dd      16384

start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti
        mov     al, byte [0x0FFE]
        mov     byte [drive], al

        mov     word [loadcur], LOAD_ADDR

        mov     word [dirsec], 0
.dirscan:
        mov     ax, word [dirsec]
        add     ax, ROOT_LBA
        mov     word [bufaddr], DIR_BUF
        call    readsec
        jc      fail
        mov     bx, DIR_BUF
        mov     di, 16
.entry:
        cmp     byte [bx], 0x00
        je      fail
        cmp     byte [bx], 0xE5
        je      .next
        push    si
        push    bx
        push    di
        mov     si, fname
        mov     di, bx
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     bx
        pop     si
        je      .found
.next:
        add     bx, 32
        dec     di
        jnz     .entry
        inc     word [dirsec]
        cmp     word [dirsec], ROOT_SECTORS
        jb      .dirscan
        jmp     fail

.found:
        mov     ax, word [bx+26]
        mov     word [cluster], ax

.loadloop:
        mov     ax, word [cluster]
        sub     ax, 2
        mov     dx, SPC
        mul     dx
        add     ax, DATA_LBA
        mov     si, ax
        mov     ax, word [loadcur]
        mov     word [bufaddr], ax
        mov     word [dap+0x04], ax
        mov     word [dap+0x06], 0
        mov     word [dap+0x08], si
        mov     word [dap+0x0C], 0
        mov     word [dap+0x02], SPC
        mov     si, dap
        mov     dl, byte [drive]
        mov     ah, 0x42
        int     0x13
        jc      fail
        mov     ax, SPC
        shl     ax, 9
        add     word [loadcur], ax

        mov     ax, word [cluster]
        shl     ax, 1
        mov     word [fatbyte], ax
        mov     ax, word [fatbyte]
        shr     ax, 9
        add     ax, FAT_LBA
        mov     word [bufaddr], FAT_BUF
        call    readsec
        jc      fail
        mov     ax, word [fatbyte]
        and     ax, 0x1FF
        mov     di, ax
        mov     ax, word [FAT_BUF + di]
        mov     word [cluster], ax
        cmp     ax, 0xFFF8
        jae     .doneload
        jmp     .loadloop
.doneload:
        jmp     0x0000:LOAD_ADDR

readsec:
        push    si
        mov     word [dap+0x02], 1
        mov     si, ax
        mov     ax, word [bufaddr]
        mov     word [dap+0x04], ax
        mov     word [dap+0x06], 0
        mov     word [dap+0x08], si
        mov     word [dap+0x0C], 0
        mov     si, dap
        mov     dl, byte [drive]
        mov     ah, 0x42
        int     0x13
        pop     si
        ret

fail:
        mov     si, msg
.put:
        lodsb
        test    al, al
        jz      .h
        mov     ah, 0x0E
        mov     bx, 15
        int     0x10
        jmp     .put
.h:     hlt
        jmp     .h

msg:    db      0x0D, 0x0A, "vbr: load err", 0

fname:  db      "LOADER  BIN"

        align   8
dap:    db      0x10
        db      0
        dw      0
        dw      0
        dw      0
        dd      0
        dd      0

bufaddr: dw     0
loadcur: dw     0
dirsec:  dw     0
cluster: dw     0
co:      dw     0
fatbyte: dw     0
drive:   db     0

        times   510-($-$$) db 0
        dw      0xAA55
