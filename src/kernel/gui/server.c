#include "./server.h"

#include "../initer/idt/interrupt.h"
#include "../initer/io/io.h"
#include "../initer/pit/pit.h"
#include "../lib/str/str.h"
#include "../memory/pool/pool.h"
#include "../thread/thread.h"
#include "./shm.h"
#include "./wm.h"

static struct wl_client clients[WL_MAX_CLIENTS];
static struct wl_surface surfaces[WL_MAX_SURFACES];

static struct gfx_canvas screen;
static struct gfx_canvas back;
static struct gfx_canvas *dst = &screen;
static int double_buffer = 0;
static int scrnx, scrny;

static struct lock comp_lock;
static int session_active = 0;
static int cursor_visible = 1;

struct gui_input {
    int type;
    int32_t a, b, c;
};
#define GUI_INQ_SIZE 32
static volatile struct gui_input inq[GUI_INQ_SIZE];
static volatile uint32_t inq_head = 0, inq_tail = 0;

#define MAX_DAMAGE 48
static struct gfx_rect damage[MAX_DAMAGE];
static int damage_n = 0;
static int damage_full = 0;

static int cur_x = 512, cur_y = 384;
static uint8_t last_buttons = 0;

#define CURSOR_W 12
#define CURSOR_H 18
static const char *cursor_bmp[CURSOR_H] = {
    "#...........", "##..........", "#.#.........", "#..#........",
    "#...#.......", "#....#......", "#.....#.....", "#......#....",
    "#.......#...", "#........#..", "#.....#####.", "#..#..#.....",
    "#.#...#.....", "##....#.....", "#.....#.....", ".....#......",
    "....#.......", "............",
};

static void client_post(struct wl_client *c, int type, int32_t a, int32_t b,
                        int32_t cc) {
    if (!c || !c->used)
        return;
    lock_acquire(&c->lock);
    int next = (c->qhead + 1) % WL_CLIENT_QUEUE;
    if (next != c->qtail) {
        c->queue[c->qhead].type = type;
        c->queue[c->qhead].a = a;
        c->queue[c->qhead].b = b;
        c->queue[c->qhead].c = cc;
        c->qhead = next;
        sema_up(&c->sema);
    }
    lock_release(&c->lock);
}

void (*log_hook)(const char *s) = 0;
void comp_log(const char *s) {
    if (log_hook)
        log_hook(s);
}

void comp_damage_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    struct gfx_rect r = {x, y, w, h}, scr = {0, 0, scrnx, scrny}, v;
    if (!gfx_rect_intersect(r, scr, &v))
        return;
    lock_acquire(&comp_lock);
    if (damage_full) {
        lock_release(&comp_lock);
        return;
    }

    for (int i = 0; i < damage_n; i++) {
        struct gfx_rect u;
        if (gfx_rect_intersect(v, damage[i], &u)) {
            int x0 = v.x < damage[i].x ? v.x : damage[i].x;
            int y0 = v.y < damage[i].y ? v.y : damage[i].y;
            int x1 = (v.x + v.w) > (damage[i].x + damage[i].w)
                         ? (v.x + v.w)
                         : (damage[i].x + damage[i].w);
            int y1 = (v.y + v.h) > (damage[i].y + damage[i].h)
                         ? (v.y + v.h)
                         : (damage[i].y + damage[i].h);
            damage[i].x = x0;
            damage[i].y = y0;
            damage[i].w = x1 - x0;
            damage[i].h = y1 - y0;
            lock_release(&comp_lock);
            return;
        }
    }
    if (damage_n >= MAX_DAMAGE) {
        damage_full = 1;
    } else {
        damage[damage_n++] = v;
    }
    lock_release(&comp_lock);
}

void comp_damage_surface(struct wl_surface *s) {
    if (!s || !s->used)
        return;
    int m = WIN_SHADOW + WIN_RADIUS + 2;
    comp_damage_rect(s->x - COMP_BORDER - m, s->y - COMP_TITLE_H - m,
                     s->w + 2 * COMP_BORDER + 2 * m,
                     s->h + COMP_TITLE_H + COMP_BORDER + 2 * m);
}

void comp_post_key(uint8_t scancode, int pressed, uint8_t mods) {
    uint32_t next = (inq_head + 1) % GUI_INQ_SIZE;
    if (next == inq_tail)
        return;
    inq[inq_head].type = GUI_IN_KEY;
    inq[inq_head].a = scancode;
    inq[inq_head].b = pressed;
    inq[inq_head].c = mods;
    inq_head = next;
}

void comp_post_mouse(int dx, int dy, uint8_t buttons) {
    uint32_t next = (inq_head + 1) % GUI_INQ_SIZE;
    if (next == inq_tail)
        return;
    inq[inq_head].type = GUI_IN_MOUSE;
    inq[inq_head].a = dx;
    inq[inq_head].b = dy;
    inq[inq_head].c = buttons;
    inq_head = next;
}

struct wl_client *wl_display_connect(const char *name) {
    lock_acquire(&comp_lock);
    for (int i = 0; i < WL_MAX_CLIENTS; i++) {
        if (clients[i].used)
            continue;
        struct wl_client *c = &clients[i];
        memset(c, 0, sizeof(*c));
        c->used = 1;
        strncpy(c->name, name, 15);
        c->name[15] = 0;
        sema_init(&c->sema, 0);
        lock_init(&c->lock);
        lock_release(&comp_lock);
        return c;
    }
    lock_release(&comp_lock);
    return 0;
}

void wl_display_disconnect(struct wl_client *c) {
    if (!c)
        return;
    lock_acquire(&comp_lock);
    c->used = 0;
    lock_release(&comp_lock);
}

struct wl_surface *wl_compositor_create_surface(struct wl_client *c,
                                                const char *title) {
    if (!c || !c->used)
        return 0;
    lock_acquire(&comp_lock);
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (surfaces[i].used)
            continue;
        struct wl_surface *s = &surfaces[i];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->client = c;
        s->ws = 0;
        strncpy(s->title, title, 23);
        s->title[23] = 0;
        c->surf = s;
        lock_release(&comp_lock);
        return s;
    }
    lock_release(&comp_lock);
    return 0;
}

int wl_surface_attach(struct wl_surface *s, struct shm_pool *pool, int w,
                      int h) {
    if (!s || !s->used || !pool || !pool->in_use)
        return -1;
    lock_acquire(&comp_lock);
    s->buf = pool->data;
    s->buf_w = w;
    s->buf_h = h;
    lock_release(&comp_lock);
    return 0;
}

void wl_surface_commit(struct wl_surface *s) {
    if (!s || !s->used || !s->buf)
        return;
    lock_acquire(&comp_lock);
    s->frame_pending = 1;
    lock_release(&comp_lock);
    comp_damage_surface(s);
}

void wl_surface_destroy(struct wl_surface *s) {
    if (!s || !s->used)
        return;
    lock_acquire(&comp_lock);
    if (s->client)
        s->client->surf = 0;
    s->used = 0;
    s->buf = 0;
    lock_release(&comp_lock);
}

int wl_display_dispatch(struct wl_client *c, struct wl_event *ev) {
    if (!c || !c->used)
        return -1;
    sema_down(&c->sema);
    lock_acquire(&c->lock);
    if (c->qtail == c->qhead) {
        lock_release(&c->lock);
        ev->type = WL_EV_NONE;
        return 0;
    }
    *ev = c->queue[c->qtail];
    c->qtail = (c->qtail + 1) % WL_CLIENT_QUEUE;
    lock_release(&c->lock);
    return 0;
}

struct wl_surface **comp_surfaces(int *count) {
    static struct wl_surface *list[WL_MAX_SURFACES];
    int n = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++)
        if (surfaces[i].used)
            list[n++] = &surfaces[i];
    *count = n;
    return list;
}

int comp_screen_w(void) {
    return scrnx;
}
int comp_screen_h(void) {
    return scrny;
}
void comp_set_cursor_visible(int v) {
    cursor_visible = v;
}
int comp_session_active(void) {
    return session_active;
}

void comp_send_configure(struct wl_surface *s, int w, int h) {
    client_post(s->client, WL_EV_CONFIGURE, w, h, 0);
}
void comp_send_close(struct wl_surface *s) {
    client_post(s->client, WL_EV_CLOSE, 0, 0, 0);
}
void comp_send_key(struct wl_surface *s, int scancode, int pressed, int mods) {
    client_post(s->client, WL_EV_KEY, scancode, pressed, mods);
}
void comp_request_exit(void) {
    session_active = 0;
}

void comp_destroy_surface_pool(struct wl_surface *s, struct shm_pool **pool) {
    if (!pool || !*pool)
        return;
    lock_acquire(&comp_lock);
    if (s) {
        s->buf = 0;
        s->buf_w = 0;
        s->buf_h = 0;
    }
    lock_release(&comp_lock);
    shm_pool_destroy(*pool);
    *pool = 0;
}

static uint8_t wallpaper_color(int y) {
    int t = (y * 5) / scrny;
    return gfx_rgb(0, t / 3, 1 + t / 2);
}

static void draw_wallpaper(struct gfx_rect *r) {
    for (int y = r->y; y < r->y + r->h; y++) {
        gfx_hline(dst, r->x, y, r->w, wallpaper_color(y));
    }
}

static void draw_window(struct wl_surface *s, struct gfx_rect *clip) {
    int fx = s->x - COMP_BORDER;
    int fy = s->y - COMP_TITLE_H;
    int fw = s->w + 2 * COMP_BORDER;
    int fh = s->h + COMP_TITLE_H + COMP_BORDER;
    int rad = WIN_RADIUS;

    struct gfx_rect frame = {fx, fy, fw, fh}, v;
    if (!gfx_rect_intersect(frame, *clip, &v))
        return;

    int focused = (wm_focused_surface() == s);
    uint8_t border_col = focused ? TH_FRAME_FOC : TH_FRAME_UNF;
    uint8_t title_col = focused ? TH_TITLE_FOC : TH_TITLE_UNF;

    gfx_fill_round(dst, fx - WIN_SHADOW + 3, fy + 3, fw + 2 * WIN_SHADOW - 6,
                   fh + 2 * WIN_SHADOW - 2, rad + WIN_SHADOW, TH_SHADOW);
    gfx_fill_round(dst, fx - WIN_SHADOW + 5, fy + 5, fw + 2 * WIN_SHADOW - 10,
                   fh + 2 * WIN_SHADOW - 6, rad + WIN_SHADOW - 2, TH_SHADOW2);

    gfx_fill_round(dst, fx, fy, fw, fh, rad, border_col);

    gfx_fill_round(dst, fx + COMP_BORDER, fy + COMP_BORDER,
                   fw - 2 * COMP_BORDER, fh - 2 * COMP_BORDER,
                   rad - COMP_BORDER, title_col);

    gfx_text(dst, fx + 8, fy + 1, s->title, focused ? TH_TEXT : TH_MUTED, -1);

    int bs = 12, cbx = fx + fw - 14, cby = fy + 1;
    gfx_fill_round(dst, cbx, cby, bs, bs, 5, focused ? TH_CLOSE : gfx_gray(7));
    gfx_text(dst, cbx + 2, cby, "x", TH_TEXT, -1);

    if (s->w > 0 && s->h > 0) {
        struct gfx_rect content = {s->x, s->y, s->w, s->h};
        struct gfx_rect iv;
        if (gfx_rect_intersect(content, *clip, &iv)) {
            if (s->buf && s->buf_w == s->w && s->buf_h == s->h) {
                struct gfx_canvas sc = {s->buf, s->w, s->w, s->h};
                sc.bytes = (size_t)s->buf_w * (size_t)s->buf_h;
                gfx_blit(dst, iv.x, iv.y, &sc, iv.x - s->x, iv.y - s->y, iv.w,
                         iv.h);
            } else {
                gfx_fill(dst, iv.x, iv.y, iv.w, iv.h, gfx_gray(2));
            }
        }
    }

    gfx_mask_round(dst, fx, fy, fw, fh, rad, wallpaper_color(fy + fh - 1),
                   GFX_CORNER_BL | GFX_CORNER_BR);
}

static void draw_cursor(void) {
    if (!cursor_visible)
        return;
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            char p = cursor_bmp[row][col];
            if (p == '.')
                continue;
            int px = cur_x + col, py = cur_y + row;
            if (px < 0 || px >= scrnx || py < 0 || py >= scrny)
                continue;
            dst->pixels[py * dst->pitch + px] = (p == '#') ? 0 : 15;
        }
    }
}

static int rect_covered_by_window(struct gfx_rect *r, struct wl_surface **vis,
                                  int vn) {
    for (int j = 0; j < vn; j++) {
        struct wl_surface *s = vis[j];
        if (s->w <= 0 || s->h <= 0)
            continue;
        if (r->x >= s->x && r->y >= s->y && r->x + r->w <= s->x + s->w &&
            r->y + r->h <= s->y + s->h) {
            return 1;
        }
    }
    return 0;
}

static void repaint(void) {
    lock_acquire(&comp_lock);

    struct gfx_rect rects[MAX_DAMAGE + 1];
    int n = 0;
    if (damage_full) {
        rects[0].x = 0;
        rects[0].y = 0;
        rects[0].w = scrnx;
        rects[0].h = scrny;
        n = 1;
    } else {
        for (int i = 0; i < damage_n; i++)
            rects[i] = damage[i];
        n = damage_n;
    }
    damage_n = 0;
    damage_full = 0;

    struct wl_surface *vis[WL_MAX_SURFACES];
    int vn = wm_collect_visible(vis, WL_MAX_SURFACES);

    for (int i = 0; i < n; i++) {
        struct gfx_rect *r = &rects[i];
        if (!rect_covered_by_window(r, vis, vn))
            draw_wallpaper(r);
        for (int j = 0; j < vn; j++)
            draw_window(vis[j], r);
        wm_draw_bar(dst, r);
    }

    lock_release(&comp_lock);

    draw_cursor();

    if (double_buffer) {
        for (int i = 0; i < n; i++) {
            struct gfx_rect *dr = &rects[i];
            gfx_blit(&screen, dr->x, dr->y, &back, dr->x, dr->y, dr->w, dr->h);
        }
    }

    for (int i = 0; i < vn; i++) {
        struct wl_surface *s = vis[i];
        if (s->frame_pending) {
            s->frame_pending = 0;
            client_post(s->client, WL_EV_FRAME, 0, 0, 0);
        }
    }
}

void comp_init(void) {
    scrnx = io_get_scrnx();
    scrny = io_get_scrny();
    screen.pixels = io_get_vram();
    screen.pitch = scrnx;
    screen.w = scrnx;
    screen.h = scrny;
    screen.bytes = io_get_vram_bytes();

    size_t bsz = (size_t)scrnx * (size_t)scrny;
    uint8_t *bp = get_kernel_pages(
        (uint32_t)((bsz + (size_t)PAGE_SIZE - 1) / (size_t)PAGE_SIZE));
    if (bp) {
        back.pixels = bp;
        back.pitch = scrnx;
        back.w = scrnx;
        back.h = scrny;
        back.bytes = bsz;
        double_buffer = 1;
        dst = &back;
    } else {
        double_buffer = 0;
        dst = &screen;
    }

    lock_init(&comp_lock);
    shm_init();
    memset(clients, 0, sizeof(clients));
    memset(surfaces, 0, sizeof(surfaces));
    inq_head = inq_tail = 0;
    damage_n = 0;
    damage_full = 1;
    cur_x = scrnx / 2;
    cur_y = scrny / 2;
    last_buttons = 0;
    session_active = 1;
}

static void drain_input(void) {
    while (inq_tail != inq_head) {
        struct gui_input ev;
        ev.type = inq[inq_tail].type;
        ev.a = inq[inq_tail].a;
        ev.b = inq[inq_tail].b;
        ev.c = inq[inq_tail].c;
        inq_tail = (inq_tail + 1) % GUI_INQ_SIZE;

        if (ev.type == GUI_IN_KEY) {
            wm_handle_key((uint8_t)ev.a, (int)ev.b, (uint8_t)ev.c);
        } else if (ev.type == GUI_IN_MOUSE) {
            int nx = cur_x + ev.a;
            int ny = cur_y + ev.b;
            if (nx < 0)
                nx = 0;
            if (nx >= scrnx)
                nx = scrnx - 1;
            if (ny < 0)
                ny = 0;
            if (ny >= scrny)
                ny = scrny - 1;
            if (nx != cur_x || ny != cur_y) {
                comp_damage_rect(cur_x, cur_y, CURSOR_W, CURSOR_H);
                comp_damage_rect(nx, ny, CURSOR_W, CURSOR_H);
                wm_handle_motion(nx, ny);
                cur_x = nx;
                cur_y = ny;
            }
            uint8_t btn = (uint8_t)ev.c;
            uint8_t edge = btn ^ last_buttons;
            if (edge) {
                wm_handle_button(cur_x, cur_y, btn, edge);
                last_buttons = btn;
            }
        }
    }
}

void comp_run(void) {
    while (session_active) {
        drain_input();
        if (wm_bar_check_dirty()) {
            comp_damage_rect(0, 0, scrnx, COMP_BAR_H);
        }
        if (damage_full || damage_n > 0) {
            repaint();
        }
        mtime_sleep(20);
    }
}
