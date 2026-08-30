#include "kernel/gui/clients.h"

#include "drivers/char/keyboard.h"
#include "arch/x86/interrupt/interrupt.h"
#include "lib/str/str.h"
#include "kernel/sched/thread.h"
#include "kernel/gui/gfx.h"
#include "kernel/gui/server.h"
#include "kernel/gui/shm.h"
#include "kernel/gui/wm.h"

struct demo_client {
    struct wl_client *conn;
    struct wl_surface *surf;
    struct shm_pool *pool;
    int w, h;
    uint32_t frame_interval;
    uint32_t last_frame;
    void (*render)(struct demo_client *dc);
    void (*on_key)(struct demo_client *dc, int scancode, int mods);
};

extern void (*log_hook)(const char *s);

static int buffer_resize(struct demo_client *dc, int w, int h) {
    if (w <= 0 || h <= 0)
        return -1;
    if (dc->pool && dc->w == w && dc->h == h)
        return 0;
    if (dc->pool) {
        comp_destroy_surface_pool(dc->surf, &dc->pool);
    }
    dc->pool = shm_pool_create((uint32_t)(w * h));
    if (!dc->pool)
        return -1;
    dc->w = w;
    dc->h = h;
    return 0;
}

static void attach_commit(struct demo_client *dc) {
    if (!dc->pool)
        return;
    wl_surface_attach(dc->surf, dc->pool, dc->w, dc->h);
    wl_surface_commit(dc->surf);
}

static void client_main(struct demo_client *dc) {
    for (;;) {
        struct wl_event ev;
        if (wl_display_dispatch(dc->conn, &ev) != 0)
            break;
        switch (ev.type) {
        case WL_EV_CONFIGURE:
            if (buffer_resize(dc, (int)ev.a, (int)ev.b) == 0) {
                dc->render(dc);
                attach_commit(dc);
            }
            break;
        case WL_EV_FRAME:
            if (dc->pool && tick - dc->last_frame >= dc->frame_interval) {
                dc->last_frame = tick;
                dc->render(dc);
                wl_surface_commit(dc->surf);
            }
            break;
        case WL_EV_KEY:
            if (dc->on_key && ev.b)
                dc->on_key(dc, (int)ev.a, (int)ev.c);
            break;
        case WL_EV_CLOSE:
            goto out;
        default:
            break;
        }
    }
out:
    wm_unmanage(dc->surf);
    wl_surface_destroy(dc->surf);
    if (dc->pool)
        comp_destroy_surface_pool(dc->surf, &dc->pool);
    wl_display_disconnect(dc->conn);
    dc->conn = 0;
    thread_exit_current();
}

#define TERM_LINES 40
#define TERM_COLS 96
static char term_buf[TERM_LINES][TERM_COLS];
static int term_head = 0;
static int term_count = 0;
static int term_col = 0;

static void term_newline(void) {
    term_head = (term_head + 1) % TERM_LINES;
    if (term_count < TERM_LINES)
        term_count++;
    memset(term_buf[term_head], 0, TERM_COLS);
    term_col = 0;
}

static void term_putc(char ch) {
    if (ch == '\n') {
        term_newline();
        return;
    }
    if (term_col >= TERM_COLS - 1)
        term_newline();
    term_buf[term_head][term_col++] = ch;
}

static void term_puts(const char *s) {
    while (*s)
        term_putc(*s++);
}

static void term_log_hook(const char *s) {
    term_puts("[log] ");
    term_puts(s);
    term_putc('\n');
}

static void term_render(struct demo_client *dc) {
    struct gfx_canvas cv = {dc->pool->data, dc->w, dc->w, dc->h};
    gfx_fill(&cv, 0, 0, dc->w, dc->h, gfx_gray(1));
    gfx_fill(&cv, 0, 0, dc->w, 2, gfx_rgb(0, 3, 0));

    int rows = dc->h / 16;
    int cols = dc->w / 8;
    if (rows > TERM_LINES)
        rows = TERM_LINES;
    if (cols > TERM_COLS - 1)
        cols = TERM_COLS - 1;

    int start = term_head - term_count + 1;
    if (start < 0)
        start += TERM_LINES;
    int first = term_count - rows;
    if (first < 0)
        first = 0;
    for (int r = first; r < term_count; r++) {
        int li = (start + r) % TERM_LINES;
        char line[TERM_COLS];
        int n = 0;
        while (n < cols && term_buf[li][n]) {
            line[n] = term_buf[li][n];
            n++;
        }
        line[n] = 0;
        gfx_text(&cv, 4, (r - first) * 16 + 2, line, gfx_rgb(0, 4, 0), -1);
    }
    if ((tick / 25) & 1) {
        gfx_fill(&cv, 4 + term_col * 8, (term_count - first - 1) * 16 + 2, 8,
                 16, gfx_rgb(0, 4, 0));
    }
}

static void term_on_key(struct demo_client *dc, int scancode, int mods) {
    (void)mods;
    char ch = keyboard_translate((uint8_t)scancode, mods & MOD_SHIFT);
    if (!ch)
        return;
    if (ch == '\b') {
        if (term_col > 0)
            term_buf[term_head][--term_col] = 0;
    } else {
        term_putc(ch);
    }
    dc->render(dc);
    wl_surface_commit(dc->surf);
}

static void term_thread(void *arg) {
    (void)arg;
    struct demo_client dc;
    memset(&dc, 0, sizeof(dc));
    dc.conn = wl_display_connect("term");
    if (!dc.conn) {
        thread_exit_current();
        return;
    }
    dc.surf =
        wl_compositor_create_surface(dc.conn, "term - wayland-ish client");
    if (!dc.surf) {
        wl_display_disconnect(dc.conn);
        thread_exit_current();
        return;
    }
    dc.render = term_render;
    dc.on_key = term_on_key;
    dc.frame_interval = 25;
    log_hook = term_log_hook;
    wm_manage(dc.surf);
    client_main(&dc);
    log_hook = 0;
}

static void u32_to_str(uint32_t v, char *buf) {
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    } else {
        char tmp[12];
        int m = 0;
        while (v) {
            tmp[m++] = (char)('0' + v % 10);
            v /= 10;
        }
        while (m--)
            buf[n++] = tmp[m];
    }
    buf[n] = 0;
}

static void clock_render(struct demo_client *dc) {
    struct gfx_canvas cv = {dc->pool->data, dc->w, dc->w, dc->h};
    gfx_fill(&cv, 0, 0, dc->w, dc->h, gfx_rgb(0, 0, 1));

    uint32_t secs = tick / 100;
    uint32_t hh = (secs / 3600) % 24;
    uint32_t mm = (secs / 60) % 60;
    uint32_t ss = secs % 60;
    char tbuf[12];
    tbuf[0] = (char)('0' + hh / 10);
    tbuf[1] = (char)('0' + hh % 10);
    tbuf[2] = ':';
    tbuf[3] = (char)('0' + mm / 10);
    tbuf[4] = (char)('0' + mm % 10);
    tbuf[5] = ':';
    tbuf[6] = (char)('0' + ss / 10);
    tbuf[7] = (char)('0' + ss % 10);
    tbuf[8] = 0;

    int scale = dc->w / 340;
    if (scale < 2)
        scale = 2;
    if (scale > 6)
        scale = 6;
    int tw = 8 * 8 * scale;
    int th = 16 * scale;
    int x = (dc->w - tw) / 2;
    int y = (dc->h - th) / 2;
    gfx_text_scaled(&cv, x, y, tbuf, gfx_rgb(3, 5, 5), -1, scale);

    gfx_text(&cv, 8, 8, "frame-callback driven clock", gfx_gray(12), -1);
    gfx_text(&cv, 8, dc->h - 20, "uptime since boot", gfx_gray(10), -1);
}

static void clock_thread(void *arg) {
    (void)arg;
    struct demo_client dc;
    memset(&dc, 0, sizeof(dc));
    dc.conn = wl_display_connect("clock");
    if (!dc.conn) {
        thread_exit_current();
        return;
    }
    dc.surf = wl_compositor_create_surface(dc.conn, "clock");
    if (!dc.surf) {
        wl_display_disconnect(dc.conn);
        thread_exit_current();
        return;
    }
    dc.render = clock_render;
    dc.frame_interval = 20;
    wm_manage(dc.surf);
    client_main(&dc);
}

static void sysmon_render(struct demo_client *dc) {
    struct gfx_canvas cv = {dc->pool->data, dc->w, dc->w, dc->h};
    gfx_fill(&cv, 0, 0, dc->w, dc->h, gfx_gray(2));

    gfx_text(&cv, 8, 8, "sysmon - compositor stats", 15, -1);

    int nsurf = 0;
    comp_surfaces(&nsurf);
    char line[64];
    char num[12];

    line[0] = 0;
    strcat(line, "surfaces: ");
    u32_to_str((uint32_t)nsurf, num);
    strcat(line, num);
    gfx_text(&cv, 8, 32, line, gfx_gray(18), -1);

    line[0] = 0;
    strcat(line, "workspace: ");
    u32_to_str((uint32_t)(wm_current_ws() + 1), num);
    strcat(line, num);
    strcat(line, " / 4");
    gfx_text(&cv, 8, 48, line, gfx_gray(18), -1);

    line[0] = 0;
    strcat(line, "tick: ");
    u32_to_str(tick, num);
    strcat(line, num);
    gfx_text(&cv, 8, 64, line, gfx_gray(18), -1);

    int gx = 8, gy = 88, gw = dc->w - 16, gh = dc->h - 100;
    if (gw > 8 && gh > 8) {
        gfx_rect(&cv, gx, gy, gw, gh, gfx_gray(10));
        int bars = (gw - 4) / 6;
        for (int i = 0; i < bars; i++) {
            uint32_t v = (tick / 4 + (uint32_t)i * 7) % 40;
            int bh = (int)(v * (uint32_t)(gh - 6) / 40);
            uint8_t col = gfx_rgb(0, 2 + (int)(v % 4), 2);
            gfx_fill(&cv, gx + 2 + i * 6, gy + gh - 2 - bh, 4, bh, col);
        }
    }
}

static void sysmon_thread(void *arg) {
    (void)arg;
    struct demo_client dc;
    memset(&dc, 0, sizeof(dc));
    dc.conn = wl_display_connect("sysmon");
    if (!dc.conn) {
        thread_exit_current();
        return;
    }
    dc.surf = wl_compositor_create_surface(dc.conn, "sysmon");
    if (!dc.surf) {
        wl_display_disconnect(dc.conn);
        thread_exit_current();
        return;
    }
    dc.render = sysmon_render;
    dc.frame_interval = 15;
    wm_manage(dc.surf);
    client_main(&dc);
}

static void plasma_render(struct demo_client *dc) {
    struct gfx_canvas cv = {dc->pool->data, dc->w, dc->w, dc->h};
    int phase = (int)(tick / 3);
    for (int y = 0; y < dc->h; y += 2) {
        uint8_t *row = dc->pool->data + y * dc->w;
        for (int x = 0; x < dc->w; x += 2) {
            int r = ((x + phase) / 48) % 6;
            int g = ((y + phase) / 40) % 6;
            int b = ((x + y + phase) / 64) % 6;
            uint8_t col = gfx_rgb(r, g, b);
            row[x] = col;
            if (x + 1 < dc->w)
                row[x + 1] = col;
        }
        if (y + 1 < dc->h) {
            uint8_t *dst = dc->pool->data + (y + 1) * dc->w;
            for (int i = 0; i < dc->w; i++)
                dst[i] = row[i];
        }
    }
    gfx_text(&cv, 8, 8, "plasma - shm client rendering", 15, -1);
}

static void plasma_thread(void *arg) {
    (void)arg;
    struct demo_client dc;
    memset(&dc, 0, sizeof(dc));
    dc.conn = wl_display_connect("plasma");
    if (!dc.conn) {
        thread_exit_current();
        return;
    }
    dc.surf = wl_compositor_create_surface(dc.conn, "plasma");
    if (!dc.surf) {
        wl_display_disconnect(dc.conn);
        thread_exit_current();
        return;
    }
    dc.render = plasma_render;
    dc.frame_interval = 8;
    wm_manage(dc.surf);
    client_main(&dc);
}

typedef void (*client_thread_fn)(void *);

static client_thread_fn types[] = {term_thread, clock_thread, sysmon_thread,
                                   plasma_thread};
static const char *type_names[] = {"gc_term", "gc_clock", "gc_sysmon",
                                   "gc_plasma"};
static int next_type = 0;

void clients_spawn_next(void) {
    int t = next_type % 4;
    next_type++;
    kernel_thread((char *)type_names[t], 6, types[t], 0);
}

void clients_spawn_initial(void) {
    next_type = 0;
    clients_spawn_next();
    clients_spawn_next();
    clients_spawn_next();
}

void clients_broadcast_close(void) {
    int n = 0;
    struct wl_surface **list = comp_surfaces(&n);
    for (int i = 0; i < n; i++)
        comp_send_close(list[i]);
}
