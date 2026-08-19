#include <stdint.h>
#include <stddef.h>

#define FONT_BASE ((const uint8_t*)0x9C000)



void initIO(uint8_t* vram, int scrnx, int scrny, uint32_t vram_bytes);
void setTextColor(int color);
void setCursor(int x, int y);
int  getCursorX(void);
int  getCursorY(void);

void kprintf(const char* fmt, ...);

void console_putc(char c);
void console_put_str(const char* s);

void io_clear_screen(void);    

void showChar(uint8_t* vram, int pitch, int x, int y, int scrnx, int scrny, char c, int color, int bg);
void showString(uint8_t* vram, int pitch, int x, int y, int scrnx, int scrny, const char* s, int color, int bg);

void initPalette(void);


uint8_t* io_get_vram(void);
int      io_get_scrnx(void);
int      io_get_scrny(void);
size_t   io_get_vram_bytes(void);     
void     io_set_gui_active(int on);   
