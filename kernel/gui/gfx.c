#include "kernel/gui/gfx.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/asmFunc.h"
#include "drivers/char/console/io.h"

uint8_t gfx_rgb(int r, int g, int b) {
    if (r < 0)
        r = 0;
    if (r > 5)
        r = 5;
    if (g < 0)
        g = 0;
    if (g > 5)
        g = 5;
    if (b < 0)
        b = 0;
    if (b > 5)
        b = 5;
    return (uint8_t)(16 + 36 * r + 6 * g + b);
}

uint8_t gfx_gray(int level) {
    if (level < 0)
        level = 0;
    if (level > 23)
        level = 23;
    return (uint8_t)(232 + level);
}

void gfx_init_palette(void) {
    static const uint8_t sys16[16][3] = {
        {0, 0, 0},     {0, 0, 170},    {0, 170, 0},    {0, 170, 170},
        {170, 0, 0},   {170, 0, 170},  {170, 85, 0},   {170, 170, 170},
        {85, 85, 85},  {85, 85, 255},  {85, 255, 85},  {85, 255, 255},
        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
    static const uint8_t cube_lvl[6] = {0, 95, 135, 175, 215, 255};

    outb(0x03C8, 0);
    for (int i = 0; i < 16; i++) {
        outb(0x03C9, (uint8_t)(sys16[i][0] >> 2));
        outb(0x03C9, (uint8_t)(sys16[i][1] >> 2));
        outb(0x03C9, (uint8_t)(sys16[i][2] >> 2));
    }
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                outb(0x03C9, (uint8_t)(cube_lvl[r] >> 2));
                outb(0x03C9, (uint8_t)(cube_lvl[g] >> 2));
                outb(0x03C9, (uint8_t)(cube_lvl[b] >> 2));
            }
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(8 + i * 10);
        outb(0x03C9, (uint8_t)(v >> 2));
        outb(0x03C9, (uint8_t)(v >> 2));
        outb(0x03C9, (uint8_t)(v >> 2));
    }
}

int gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b,
                       struct gfx_rect *out) {
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return 0;
    if (out) {
        out->x = x0;
        out->y = y0;
        out->w = x1 - x0;
        out->h = y1 - y0;
    }
    return 1;
}

static int gfx_isqrt(int v) {
    if (v <= 0)
        return 0;
    int root = 0;
    for (int bit = 1 << 15; bit > 0; bit >>= 1) {
        if (root + bit <= 46340) {
            int candidate = root + bit;
            if (candidate * candidate <= v)
                root = candidate;
        }
    }
    return root;
}

void gfx_fill_round(struct gfx_canvas *c, int x, int y, int w, int h, int rad,
                    uint8_t color) {
    if (!c || !c->pixels)
        return;
    struct gfx_rect clip = {0, 0, c->w, c->h};
    struct gfx_rect r = {x, y, w, h}, v;
    if (!gfx_rect_intersect(clip, r, &v))
        return;
    if (v.w <= 0 || v.h <= 0)
        return;
    if (rad < 0)
        rad = 0;
    if (rad * 2 > v.w)
        rad = v.w / 2;
    if (rad * 2 > v.h)
        rad = v.h / 2;
    int ok;
    size_t mapped = gfx_canvas_mapped_bytes(c, &ok);
    if (!ok || mapped == 0)
        return;
    for (int row = 0; row < v.h; row++) {
        int cy;
        if (row < rad)
            cy = rad;
        else if (row >= v.h - rad)
            cy = v.h - rad;
        else
            cy = -1;
        int inset = 0;
        if (cy >= 0) {
            int d = (cy == rad) ? (rad - row) : (row - cy);
            int halfw = (d >= rad) ? 0 : gfx_isqrt(rad * rad - d * d);
            inset = rad - halfw;
        }
        int x0 = v.x + inset;
        int x1 = v.x + v.w - inset;
        if (x1 <= x0)
            continue;
        size_t off = (size_t)(v.y + row) * (size_t)c->pitch + (size_t)x0;
        size_t end = off + (size_t)(x1 - x0);
        if (end > mapped)
            continue;
        uint8_t *p = c->pixels + off;
        for (int i = x0; i < x1; i++)
            p[i - x0] = color;
    }
}

void gfx_mask_round(struct gfx_canvas *c, int x, int y, int w, int h, int rad,
                    uint8_t color, int corners) {
    if (!c || !c->pixels || !corners)
        return;
    struct gfx_rect clip = {0, 0, c->w, c->h};
    struct gfx_rect r = {x, y, w, h}, v;
    if (!gfx_rect_intersect(clip, r, &v))
        return;
    if (v.w <= 0 || v.h <= 0)
        return;
    if (rad < 0)
        rad = 0;
    if (rad * 2 > v.w)
        rad = v.w / 2;
    if (rad * 2 > v.h)
        rad = v.h / 2;
    for (int row = 0; row < v.h; row++) {
        int cy;
        if (row < rad)
            cy = rad;
        else if (row >= v.h - rad)
            cy = v.h - rad;
        else
            cy = -1;
        if (cy < 0)
            continue;
        int is_top = (cy == rad);
        int cbits = is_top ? (corners & (GFX_CORNER_TL | GFX_CORNER_TR))
                           : (corners & (GFX_CORNER_BL | GFX_CORNER_BR));
        if (!cbits)
            continue;
        int d = is_top ? (rad - row) : (row - cy);
        int halfw = (d >= rad) ? 0 : gfx_isqrt(rad * rad - d * d);
        int inset = rad - halfw;
        if (inset <= 0)
            continue;
        if (cbits & GFX_CORNER_TL)
            gfx_fill(c, v.x, v.y + row, inset, 1, color);
        if (cbits & GFX_CORNER_TR)
            gfx_fill(c, v.x + v.w - inset, v.y + row, inset, 1, color);
    }
}

void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, uint8_t color) {
    struct gfx_rect clip = {0, 0, c->w, c->h};
    struct gfx_rect r = {x, y, w, h}, v;
    if (!gfx_rect_intersect(clip, r, &v))
        return;
    if (!c->pixels)
        return;
    int ok;
    size_t mapped = gfx_canvas_mapped_bytes(c, &ok);
    if (!ok || mapped == 0)
        return;
    size_t last;
    if (__builtin_mul_overflow((size_t)v.y + (size_t)v.h - 1, (size_t)c->pitch,
                               &last))
        return;
    if (__builtin_add_overflow(last, (size_t)v.x + (size_t)v.w - 1, &last))
        return;
    if (last >= mapped)
        return;
    for (int row = 0; row < v.h; row++) {
        uint8_t *p = c->pixels + (v.y + row) * c->pitch + v.x;
        for (int i = 0; i < v.w; i++)
            p[i] = color;
    }
}

void gfx_hline(struct gfx_canvas *c, int x, int y, int len, uint8_t color) {
    gfx_fill(c, x, y, len, 1, color);
}

void gfx_vline(struct gfx_canvas *c, int x, int y, int len, uint8_t color) {
    gfx_fill(c, x, y, 1, len, color);
}

void gfx_rect(struct gfx_canvas *c, int x, int y, int w, int h, uint8_t color) {
    gfx_hline(c, x, y, w, color);
    gfx_hline(c, x, y + h - 1, w, color);
    gfx_vline(c, x, y, h, color);
    gfx_vline(c, x + w - 1, y, h, color);
}

void gfx_char(struct gfx_canvas *c, int x, int y, char ch, uint8_t fg, int bg) {
    const uint8_t *font = FONT_BASE + ((uint8_t)ch) * 16;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = font[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col, py = y + row;
            if (px < 0 || px >= c->w || py < 0 || py >= c->h)
                continue;
            if (bits & (0x80 >> col))
                c->pixels[py * c->pitch + px] = fg;
            else if (bg >= 0)
                c->pixels[py * c->pitch + px] = (uint8_t)bg;
        }
    }
}

void gfx_text(struct gfx_canvas *c, int x, int y, const char *s, uint8_t fg,
              int bg) {
    while (*s) {
        gfx_char(c, x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

void gfx_char_scaled(struct gfx_canvas *c, int x, int y, char ch, uint8_t fg,
                     int bg, int scale) {
    if (scale < 1)
        scale = 1;
    const uint8_t *font = FONT_BASE + ((uint8_t)ch) * 16;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = font[row];
        for (int col = 0; col < 8; col++) {
            int on = bits & (0x80 >> col);
            if (!on && bg < 0)
                continue;
            gfx_fill(c, x + col * scale, y + row * scale, scale, scale,
                     on ? fg : (uint8_t)bg);
        }
    }
}

void gfx_text_scaled(struct gfx_canvas *c, int x, int y, const char *s,
                     uint8_t fg, int bg, int scale) {
    while (*s) {
        gfx_char_scaled(c, x, y, *s, fg, bg, scale);
        x += 8 * scale;
        s++;
    }
}

void gfx_blit(struct gfx_canvas *dst, int dx, int dy,
              const struct gfx_canvas *src, int sx, int sy, int w, int h) {
    if (dx < 0) {
        sx -= dx;
        w += dx;
        dx = 0;
    }
    if (dy < 0) {
        sy -= dy;
        h += dy;
        dy = 0;
    }
    if (dx + w > dst->w)
        w = dst->w - dx;
    if (dy + h > dst->h)
        h = dst->h - dy;
    if (w <= 0 || h <= 0)
        return;

    if (sx < 0) {
        dx -= sx;
        w += sx;
        sx = 0;
    }
    if (sy < 0) {
        dy -= sy;
        h += sy;
        sy = 0;
    }
    if (sx + w > src->w)
        w = src->w - sx;
    if (sy + h > src->h)
        h = src->h - sy;
    if (w <= 0 || h <= 0)
        return;

    if (src->w <= 0 || src->h <= 0 || src->pitch <= 0 || dst->w <= 0 ||
        dst->h <= 0 || dst->pitch <= 0)
        return;
    if (src->pitch < src->w || dst->pitch < dst->w)
        return;
    if (!dst->pixels || !src->pixels)
        return;

    int ok;
    size_t src_size = gfx_canvas_mapped_bytes(src, &ok);
    if (!ok || src_size == 0)
        return;
    size_t dst_size = gfx_canvas_mapped_bytes(dst, &ok);
    if (!ok || dst_size == 0)
        return;

    size_t src_last, dst_last;
    if (__builtin_mul_overflow((size_t)sy + (size_t)h - 1, (size_t)src->pitch,
                               &src_last))
        return;
    if (__builtin_add_overflow(src_last, (size_t)sx + (size_t)w - 1, &src_last))
        return;
    if (src_last >= src_size)
        return;

    if (__builtin_mul_overflow((size_t)dy + (size_t)h - 1, (size_t)dst->pitch,
                               &dst_last))
        return;
    if (__builtin_add_overflow(dst_last, (size_t)dx + (size_t)w - 1, &dst_last))
        return;
    if (dst_last >= dst_size)
        return;

    for (int row = 0; row < h; row++) {
        size_t d_off = (size_t)(dy + row) * (size_t)dst->pitch + (size_t)dx;
        size_t s_off = (size_t)(sy + row) * (size_t)src->pitch + (size_t)sx;
        uint8_t *d = dst->pixels + d_off;
        const uint8_t *s = src->pixels + s_off;
        for (int i = 0; i < w; i++)
            d[i] = s[i];
    }
}
