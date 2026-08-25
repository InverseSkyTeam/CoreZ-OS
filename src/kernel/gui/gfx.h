#ifndef GUI_GFX_H
#define GUI_GFX_H

#include <stddef.h>
#include <stdint.h>

struct gfx_canvas {
    uint8_t *pixels;
    int pitch;
    int w, h;
    size_t bytes;
};

static inline size_t gfx_canvas_mapped_bytes(const struct gfx_canvas *c,
                                             int *ok) {
    *ok = 1;
    if (c->bytes > 0)
        return c->bytes;
    size_t s;
    if (__builtin_mul_overflow((size_t)c->pitch, (size_t)c->h, &s)) {
        *ok = 0;
        return 0;
    }
    return s;
}

struct gfx_rect {
    int x, y, w, h;
};

void gfx_init_palette(void);

uint8_t gfx_rgb(int r, int g, int b);
uint8_t gfx_gray(int level);

void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, uint8_t color);
void gfx_rect(struct gfx_canvas *c, int x, int y, int w, int h, uint8_t color);
void gfx_hline(struct gfx_canvas *c, int x, int y, int len, uint8_t color);
void gfx_vline(struct gfx_canvas *c, int x, int y, int len, uint8_t color);

void gfx_char(struct gfx_canvas *c, int x, int y, char ch, uint8_t fg, int bg);
void gfx_text(struct gfx_canvas *c, int x, int y, const char *s, uint8_t fg,
              int bg);
void gfx_char_scaled(struct gfx_canvas *c, int x, int y, char ch, uint8_t fg,
                     int bg, int scale);
void gfx_text_scaled(struct gfx_canvas *c, int x, int y, const char *s,
                     uint8_t fg, int bg, int scale);

void gfx_blit(struct gfx_canvas *dst, int dx, int dy,
              const struct gfx_canvas *src, int sx, int sy, int w, int h);

int gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b,
                       struct gfx_rect *out);

#define GFX_CORNER_TL 1
#define GFX_CORNER_TR 2
#define GFX_CORNER_BL 4
#define GFX_CORNER_BR 8
#define GFX_CORNER_ALL                                                         \
    (GFX_CORNER_TL | GFX_CORNER_TR | GFX_CORNER_BL | GFX_CORNER_BR)

void gfx_fill_round(struct gfx_canvas *c, int x, int y, int w, int h, int rad,
                    uint8_t color);
void gfx_mask_round(struct gfx_canvas *c, int x, int y, int w, int h, int rad,
                    uint8_t color, int corners);

#endif
