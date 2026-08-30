; vbr.asm — P1 FAT32 启动分区的卷引导扇区 (VBR)
; 由 MBR (boot.bin) 用 LBA 读入 0x0600 并 jmp 到此。
; 职责: 从 FAT32 根目录(数据区簇链)按文件名 "LOADER  BIN" 找到 LOADER.BIN,
;       沿 32 位 FAT 链把文件读到内存 0xC200, 然后 jmp 0x0000:0xC200。
;
; 与 make_fat.fat32() 保持一致的分区布局 (绝对 LBA):
;   P1 起始 LBA = 2048
;   reserved    = 2 扇区 (VBR + FSInfo)
;   FAT 每份 127 扇区 (32 位表项), 共 2 份
;   数据区起始 = 2048+2+2*127 = 2304 ; 簇2(根目录)对应该 LBA
;   每簇 1 扇区 (SPC=1)

ORG     0x0600

%define PART_START      2048
%define RESERVED_SECT   2
%define FAT_SECTORS     127
%define FAT_COUNT       2
%define SPC             1
%define ROOT_CLUSTER    2

FAT_LBA   equ PART_START + RESERVED_SECT
DATA_LBA  equ FAT_LBA + FAT_COUNT*FAT_SECTORS
LOAD_ADDR equ 0xC200

DIR_BUF equ 0x0800
FAT_BUF equ 0x0A00

        jmp     short start
        nop

        db      "NITIAN  "      ; +3  OEM
        dw      512             ; +11 bytes/sector
        db      SPC             ; +13 sectors/cluster
        dw      RESERVED_SECT   ; +14 reserved sectors
        db      FAT_COUNT       ; +16 number of FATs
        dw      0               ; +19 root entries (0 for FAT32)
        dw      0               ; +21 total16 (0)
        db      0xF8            ; +22 media
        dw      0               ; +23 sectors/FAT16 (0 for FAT32)
        dw      18              ; +24 sectors/track
        dw      2               ; +26 heads
        dd      PART_START      ; +28 hidden sectors
        dd      16384           ; +32 total32 
        dd      FAT_SECTORS     ; +36 sectors/FAT32
        dw      0               ; +40 ext flags
        dw      0               ; +42 FS version
        dd      ROOT_CLUSTER    ; +44 root cluster
        dw      1               ; +48 FSInfo
        dw      0               ; +50 backup boot
        times 12 db 0           ; +52 reserved[12]
        db      0x80            ; +64 drive number
        db      0               ; +65 reserved
        db      0x29            ; +66 boot signature
        dd      0x12345678      ; +67 volume id
        db      "NITIAN    "    ; +71 volume label (11)
        db      "FAT32   "      ; +82 fs type (8)

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

        mov     word [curclu], ROOT_CLUSTER
.dirscan:
        mov     ax, word [curclu]
        sub     ax, 2
        add     ax, DATA_LBA             ; lba = DATA_LBA + (clu-2)*SPC, SPC=1
        mov     word [bufaddr], DIR_BUF
        call    readsec
        jc      fail
        mov     bx, DIR_BUF
        mov     di, 16
.entry:
        cmp     byte [bx], 0x00
        je      .nextroot
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

.nextroot:
        mov     ax, word [curclu]
        shl     ax, 2
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
        mov     eax, dword [FAT_BUF + di]
        and     eax, 0x0FFFFFFF
        mov     word [curclu], ax      
        cmp     eax, 0x0FFFFFF8
        jae     fail                    
        jmp     .dirscan

.found:
        mov     ax, word [bx+26]
        mov     word [curclu], ax

.loadloop:
        mov     ax, word [curclu]
        sub     ax, 2
        mov     dx, SPC
        mul     dx
        add     ax, DATA_LBA
        mov     word [dap+0x08], ax
        mov     word [dap+0x0C], 0
        mov     ax, word [loadcur]
        mov     word [bufaddr], ax
        mov     word [dap+0x04], ax
        mov     word [dap+0x06], 0
        mov     word [dap+0x02], SPC    
        mov     si, dap
        mov     dl, byte [drive]
        mov     ah, 0x42
        int     0x13
        jc      fail
        mov     ax, SPC
        shl     ax, 9
        add     word [loadcur], ax

        mov     ax, word [curclu]
        shl     ax, 2
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
        mov     eax, dword [FAT_BUF + di]
        and     eax, 0x0FFFFFFF
        mov     word [curclu], ax
        cmp     eax, 0x0FFFFFF8
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
curclu:  dw     0
fatbyte: dw     0
drive:   db     0

        times   510-($-$$) db 0
        dw      0xAA55