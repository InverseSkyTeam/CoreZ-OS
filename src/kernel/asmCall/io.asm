bits 64
section .text

global inb
inb:                            ; uint8_t inb(uint16_t port)
    mov     dx, di
    in      al, dx
    ret

global outb
outb:                           ; void outb(uint16_t port, uint8_t value)
    mov     dx, di
    mov     al, sil
    out     dx, al
    ret

global insw
insw:                           ; void insw(uint16_t port, void* buf, int words)
    mov     ecx, edx          
    mov     dx, di
    mov     rdi, rsi
    cld
    rep     insw
    ret

global outsw
outsw:                          ; void outsw(uint16_t port, const void* buf, int words)
    mov     ecx, edx   
    mov     dx, di
    cld
    rep     outsw
    ret