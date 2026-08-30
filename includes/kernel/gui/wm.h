#ifndef GUI_WM_H
#define GUI_WM_H

#include "kernel/gui/server.h"
#include <stdint.h>

void wm_init_state(void);

void wm_handle_key(uint8_t scancode, int pressed, uint8_t mods);
void wm_handle_motion(int x, int y);
void wm_handle_button(int x, int y, uint8_t buttons, uint8_t edge);

void wm_manage(struct wl_surface *s);
void wm_unmanage(struct wl_surface *s);

int wm_collect_visible(struct wl_surface **out, int max);
struct wl_surface *wm_focused_surface(void);
int wm_current_ws(void);
void wm_draw_bar(struct gfx_canvas *c, struct gfx_rect *clip);
int wm_bar_check_dirty(void);

#endif
