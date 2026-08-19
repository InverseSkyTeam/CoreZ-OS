// 参考: 《操作系统真相还原》(于渊) 第11章 输入输出系统
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "./ioqueue.h"

extern struct ioqueue keyboard_ioq;


#define KBD_MOD_SHIFT 1
#define KBD_MOD_CTRL  2
#define KBD_MOD_ALT   4



typedef void (*kbd_gui_hook_t)(uint8_t scancode, int pressed, uint8_t mods);

void keyboard_init(void);
void keyboard_handler(void);
void keyboard_set_gui_hook(kbd_gui_hook_t hook);
char keyboard_translate(uint8_t scancode, int shift);

#endif
