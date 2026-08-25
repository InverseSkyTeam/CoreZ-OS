; AP trampoline: 由 BSP 的 INIT/SIPI 从实模式 0x9000 唤醒。
; 关键设计: 复用 loader 为 BSP 建立的共享 64 位页表 (PML4@0x90000, 恒等
; 映射 0-12MB + 高半 1GB@0xC0000000), AP 不再重建页表。
; 依次经历: 实模式 -> 32 位兼容(PE) -> 长模式(PAE+EFER.LME+PG) -> 64 位代码段,
; 最后加载内核为本 AP 预建好的 GDT(含 percpu gs), 跳入 C 侧 ap_main(idx)。
[bits 16]
org 0x9000

start16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; [AP-DBG] 实模式入口标记 (内存 0x7000, 经 monitor 查看)
    mov     byte [0x7000], 0x41        ; 'A'

    lgdt [_tmp_gdtr]                ; 临时 GDT, 先进入 32 位兼容模式
    mov eax, cr0
    or  eax, 1                      ; PE
    mov cr0, eax
    jmp 0x18:start32

[bits 32]
start32:
    ; [AP-DBG] 进入 32 位兼容模式 (PE) 标记
    mov     byte [0x7001], 0x42        ; 'B'

    mov ax, 0x10                    ; 临时数据段
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7c00

    ; 复用 loader 页表: 恒等 0-12MB + 高半 1GB(内核), CR3 = PML4@0x90000
    mov eax, 0x90000
    mov cr3, eax

    mov eax, cr4
    or  eax, 0x20                   ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080             ; EFER
    rdmsr
    or  eax, 0x100                  ; LME
    wrmsr

    mov eax, cr0
    or  eax, 0x80000001             ; PG | PE (使能 LMA)
    mov cr0, eax
    jmp 0x08:start64                ; 切到 64 位代码段

[bits 64]
start64:
    ; [AP-DBG] 进入 64 位长模式标记
    mov     byte [0x7002], 0x43        ; 'C'

    ; 由 ap_boot_info(@0x8000) 依次取得: gdtr, stack_top, ap_main, index
    ; ap_boot_info 是 4xuint32 紧凑结构(偏移 0/4/8/0xC), 必须用 32 位加载
    ; (零扩展), 不能用 8 字节 mov 否则会串位读到相邻字段, lgdt 拿到错误指针
    ; 而 #GP。内核高半 VA(0xC0xxxxxx) < 4GB, uint32 放得下。
    mov rax, 0x8000
    mov ebx, [rax + 0x00]           ; gdtr base (内核高半 VA, uint32 零扩展)
    mov edx, [rax + 0x04]           ; stack_top (低端物理, 须落在恒等映射内)
    mov ecx, [rax + 0x08]           ; ap_main (内核高半 VA)
    mov r10d, [rax + 0x0C]          ; index
    ; (mov edx/ecx/r10d/ebx 写 32 位寄存器都会自动零扩展成 64 位指针)

    lgdt [rbx]                      ; 换到内核为本 AP 预建的 64 位 GDT

    ; [AP-DBG] GDT 加载后标记
    mov     byte [0x7003], 0x44        ; 'D'

    mov ax, 0x10                    ; 内核数据段
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov ax, 0x40                    ; percpu gs
    mov gs, ax

    mov rdi, r10                    ; ap_main 第 1 参 idx (SysV)
    mov rsp, rdx
    call rcx
.hang:
    cli
    hlt
    jmp .hang

align 8
_tmp_gdt:
    dq 0                                                ; 0x00 null
    db 0xff,0xff,0x00,0x00,0x00,0x9a,0x20,0x00          ; 0x08 64-bit code (L=1)
    db 0xff,0xff,0x00,0x00,0x00,0x92,0xcf,0x00          ; 0x10 data
    db 0xff,0xff,0x00,0x00,0x00,0x9a,0xcf,0x00          ; 0x18 32-bit compat code
_tmp_gdtr:
    dw (_tmp_gdt_end - _tmp_gdt - 1)
    dd _tmp_gdt
_tmp_gdt_end: