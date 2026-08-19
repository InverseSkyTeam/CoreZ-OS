
// 参考: OSDev Wiki "Mouse Input" / "PS/2 Mouse"
#include "./mouse.h"
#include "../include/asmFunc.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define KBD_CMD    0x64

static mouse_hook_t g_hook = 0;
static uint8_t g_pkt[3];
static int g_idx = 0;

static void ctrl_wait_write(void) {
    while (inb(KBD_STATUS) & 0x02) {
    }
}

static void ctrl_wait_read(void) {
    while (!(inb(KBD_STATUS) & 0x01)) {
    }
}

static void aux_write(uint8_t v) {
    ctrl_wait_write();
    outb(KBD_CMD, 0xD4);      
    ctrl_wait_write();
    outb(KBD_DATA, v);
}

static uint8_t aux_read(void) {
    ctrl_wait_read();
    return inb(KBD_DATA);
}

void mouse_set_hook(mouse_hook_t hook) {
    g_hook = hook;
}

void mouse_init(void) {
    ctrl_wait_write();
    outb(KBD_CMD, 0xA8);              

    
    ctrl_wait_write();
    outb(KBD_CMD, 0x20);
    ctrl_wait_read();
    uint8_t status = inb(KBD_DATA);
    status |= 0x02;                   
    status &= (uint8_t)~0x20;         
    ctrl_wait_write();
    outb(KBD_CMD, 0x60);
    ctrl_wait_write();
    outb(KBD_DATA, status);

    aux_write(0xF6);                  
    (void)aux_read();                 
    aux_write(0xF4);                  
    (void)aux_read();                 
}

void mouse_handler(void) {
    
    uint8_t b = inb(KBD_DATA);

    if (g_idx == 0 && !(b & 0x08)) return;   
    g_pkt[g_idx++] = b;
    if (g_idx < 3) return;
    g_idx = 0;

    int dx = (int)g_pkt[1] - ((int)(g_pkt[0] << 4) & 0x100);
    int dy = (int)g_pkt[2] - ((int)(g_pkt[0] << 3) & 0x100);
    uint8_t buttons = g_pkt[0] & 0x07;

    if (g_hook) g_hook(dx, -dy, buttons);
}
