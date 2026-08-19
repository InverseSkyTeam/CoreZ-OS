#include "./server.h"
#include "./shm.h"
#include "./wm.h"
#include "../lib/str/str.h"
#include "../initer/io/io.h"
#include "../initer/pit/pit.h"
#include "../initer/idt/interrupt.h"
#include "../thread/thread.h"

static struct wl_client  g_clients[WL_MAX_CLIENTS];
static struct wl_surface g_surfaces[WL_MAX_SURFACES];

static struct gfx_canvas g_screen;
static int g_scrnx, g_scrny;

static struct lock g_comp_lock;
static int g_session_active = 0;
static int g_cursor_visible = 1;

struct gui_input { int type; int32_t a, b, c; };
#define GUI_INQ_SIZE 32
static volatile struct gui_input g_inq[GUI_INQ_SIZE];
static volatile uint32_t g_inq_head = 0, g_inq_tail = 0;

#define MAX_DAMAGE 16
static struct gfx_rect g_damage[MAX_DAMAGE];
static int g_damage_n = 0;
static int g_damage_full = 0;

static int g_cur_x = 512, g_cur_y = 384;
static uint8_t g_last_buttons = 0;

#define CURSOR_W 12
#define CURSOR_H 18
static const char* g_cursor_bmp[CURSOR_H] = {
    "#...........",
    "##..........",
    "#.#.........",
    "#..#........",
    "#...#.......",
    "#....#......",
    "#.....#.....",
    "#......#....",
    "#.......#...",
    "#........#..",
    "#.....#####.",
    "#..#..#.....",
    "#.#...#.....",
    "##....#.....",
    "#.....#.....",
    ".....#......",
    "....#.......",
    "............",
};

static void client_post(struct wl_client* c, int type, int32_t a, int32_t b, int32_t cc) {
    if (!c || !c->used) return;
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

void (*g_log_hook)(const char* s) = 0;
void comp_log(const char* s) {
    if (g_log_hook) g_log_hook(s);
}

void comp_damage_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    struct gfx_rect r = {x, y, w, h}, scr = {0, 0, g_scrnx, g_scrny}, v;
    if (!gfx_rect_intersect(r, scr, &v)) return;
    lock_acquire(&g_comp_lock);
    if (g_damage_full) {
        lock_release(&g_comp_lock);
        return;
    }
    if (g_damage_n >= MAX_DAMAGE) {
        g_damage_full = 1;
    } else {
        g_damage[g_damage_n++] = v;
    }
    lock_release(&g_comp_lock);
}

void comp_damage_surface(struct wl_surface* s) {
    if (!s || !s->used) return;
    comp_damage_rect(s->x - COMP_BORDER, s->y - COMP_TITLE_H,
                     s->w + 2 * COMP_BORDER,
                     s->h + COMP_TITLE_H + COMP_BORDER);
}

void comp_post_key(uint8_t scancode, int pressed, uint8_t mods) {
    uint32_t next = (g_inq_head + 1) % GUI_INQ_SIZE;
    if (next == g_inq_tail) return;
    g_inq[g_inq_head].type = GUI_IN_KEY;
    g_inq[g_inq_head].a = scancode;
    g_inq[g_inq_head].b = pressed;
    g_inq[g_inq_head].c = mods;
    g_inq_head = next;
}

void comp_post_mouse(int dx, int dy, uint8_t buttons) {
    uint32_t next = (g_inq_head + 1) % GUI_INQ_SIZE;
    if (next == g_inq_tail) return;
    g_inq[g_inq_head].type = GUI_IN_MOUSE;
    g_inq[g_inq_head].a = dx;
    g_inq[g_inq_head].b = dy;
    g_inq[g_inq_head].c = buttons;
    g_inq_head = next;
}

struct wl_client* wl_display_connect(const char* name) {
    lock_acquire(&g_comp_lock);
    for (int i = 0; i < WL_MAX_CLIENTS; i++) {
        if (g_clients[i].used) continue;
        struct wl_client* c = &g_clients[i];
        memset(c, 0, sizeof(*c));
        c->used = 1;
        strncpy(c->name, name, 15);
        c->name[15] = 0;
        sema_init(&c->sema, 0);
        lock_init(&c->lock);
        lock_release(&g_comp_lock);
        return c;
    }
    lock_release(&g_comp_lock);
    return 0;
}

void wl_display_disconnect(struct wl_client* c) {
    if (!c) return;
    lock_acquire(&g_comp_lock);
    c->used = 0;
    lock_release(&g_comp_lock);
}

struct wl_surface* wl_compositor_create_surface(struct wl_client* c, const char* title) {
    if (!c || !c->used) return 0;
    lock_acquire(&g_comp_lock);
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_surfaces[i].used) continue;
        struct wl_surface* s = &g_surfaces[i];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->client = c;
        s->ws = 0;
        strncpy(s->title, title, 23);
        s->title[23] = 0;
        c->surf = s;
        lock_release(&g_comp_lock);
        return s;
    }
    lock_release(&g_comp_lock);
    return 0;
}

int wl_surface_attach(struct wl_surface* s, struct shm_pool* pool, int w, int h) {
    if (!s || !s->used || !pool || !pool->in_use) return -1;
    lock_acquire(&g_comp_lock);
    s->buf = pool->data;
    s->buf_w = w;
    s->buf_h = h;
    lock_release(&g_comp_lock);
    return 0;
}

void wl_surface_commit(struct wl_surface* s) {
    if (!s || !s->used || !s->buf) return;
    lock_acquire(&g_comp_lock);
    s->frame_pending = 1;
    lock_release(&g_comp_lock);
    comp_damage_surface(s);
}

void wl_surface_destroy(struct wl_surface* s) {
    if (!s || !s->used) return;
    lock_acquire(&g_comp_lock);
    if (s->client) s->client->surf = 0;
    s->used = 0;
    s->buf = 0;
    lock_release(&g_comp_lock);
}

int wl_display_dispatch(struct wl_client* c, struct wl_event* ev) {
    if (!c || !c->used) return -1;
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

struct wl_surface** comp_surfaces(int* count) {
    static struct wl_surface* list[WL_MAX_SURFACES];
    int n = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++)
        if (g_surfaces[i].used) list[n++] = &g_surfaces[i];
    *count = n;
    return list;
}

int comp_screen_w(void) { return g_scrnx; }
int comp_screen_h(void) { return g_scrny; }
void comp_set_cursor_visible(int v) { g_cursor_visible = v; }
int  comp_session_active(void) { return g_session_active; }

void comp_send_configure(struct wl_surface* s, int w, int h) {
    client_post(s->client, WL_EV_CONFIGURE, w, h, 0);
}
void comp_send_close(struct wl_surface* s) {
    client_post(s->client, WL_EV_CLOSE, 0, 0, 0);
}
void comp_send_key(struct wl_surface* s, int scancode, int pressed, int mods) {
    client_post(s->client, WL_EV_KEY, scancode, pressed, mods);
}
void comp_request_exit(void) { g_session_active = 0; }

void comp_destroy_surface_pool(struct wl_surface* s, struct shm_pool** pool) {
    if (!pool || !*pool) return;
    lock_acquire(&g_comp_lock);
    if (s) {
        s->buf = 0;
        s->buf_w = 0;
        s->buf_h = 0;
    }
    lock_release(&g_comp_lock);
    shm_pool_destroy(*pool);
    *pool = 0;
}

static uint8_t wallpaper_color(int y) {
    int g = y * 2 / g_scrny;
    int b = 1 + y * 2 / g_scrny;
    return gfx_rgb(0, g, b);
}

static void draw_wallpaper(struct gfx_rect* r) {
    for (int y = r->y; y < r->y + r->h; y++) {
        gfx_hline(&g_screen, r->x, y, r->w, wallpaper_color(y));
    }
}

static void draw_window(struct wl_surface* s, struct gfx_rect* clip) {
    int fx = s->x - COMP_BORDER;
    int fy = s->y - COMP_TITLE_H;
    int fw = s->w + 2 * COMP_BORDER;
    int fh = s->h + COMP_TITLE_H + COMP_BORDER;

    struct gfx_rect frame = {fx, fy, fw, fh}, v;
    if (!gfx_rect_intersect(frame, *clip, &v)) return;

    int focused = (wm_focused_surface() == s);
    uint8_t border_col = focused ? gfx_rgb(2, 4, 5) : gfx_gray(8);
    uint8_t title_col  = focused ? gfx_rgb(1, 2, 4) : gfx_gray(4);

    gfx_fill(&g_screen, v.x, v.y, v.w, v.h, border_col);
    struct gfx_rect tr = {fx, fy, fw, COMP_TITLE_H}, tv;
    if (gfx_rect_intersect(tr, *clip, &tv)) {
        gfx_fill(&g_screen, tv.x, tv.y, tv.w, tv.h, title_col);
        gfx_text(&g_screen, fx + 6, fy + 1, s->title,
                 focused ? 15 : 7, -1);
        gfx_text(&g_screen, fx + fw - 14, fy + 1, "x",
                 focused ? 15 : 7, -1);
    }

    struct gfx_rect cr = {s->x, s->y, s->w, s->h}, cv;
    if (gfx_rect_intersect(cr, *clip, &cv)) {
        if (s->buf && s->buf_w == s->w && s->buf_h == s->h) {
            struct gfx_canvas sc = { s->buf, s->w, s->w, s->h };
            sc.bytes = (size_t)s->buf_w * (size_t)s->buf_h;
            gfx_blit(&g_screen, cv.x, cv.y, &sc,
                     cv.x - s->x, cv.y - s->y, cv.w, cv.h);
        } else {
            gfx_fill(&g_screen, cv.x, cv.y, cv.w, cv.h, gfx_gray(2));
        }
    }
}

static void draw_cursor(void) {
    if (!g_cursor_visible) return;
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            char p = g_cursor_bmp[row][col];
            if (p == '.') continue;
            int px = g_cur_x + col, py = g_cur_y + row;
            if (px < 0 || px >= g_scrnx || py < 0 || py >= g_scrny) continue;
            g_screen.pixels[py * g_screen.pitch + px] = (p == '#') ? 0 : 15;
        }
    }
}

static void repaint(void) {
    lock_acquire(&g_comp_lock);

    struct gfx_rect rects[MAX_DAMAGE + 1];
    int n = 0;
    if (g_damage_full) {
        rects[0].x = 0; rects[0].y = 0;
        rects[0].w = g_scrnx; rects[0].h = g_scrny;
        n = 1;
    } else {
        for (int i = 0; i < g_damage_n; i++) rects[i] = g_damage[i];
        n = g_damage_n;
    }
    g_damage_n = 0;
    g_damage_full = 0;

    struct wl_surface* vis[WL_MAX_SURFACES];
    int vn = wm_collect_visible(vis, WL_MAX_SURFACES);

    for (int i = 0; i < n; i++) {
        struct gfx_rect* r = &rects[i];
        draw_wallpaper(r);
        for (int j = 0; j < vn; j++) draw_window(vis[j], r);
        wm_draw_bar(&g_screen, r);
    }

    lock_release(&g_comp_lock);

    draw_cursor();

    for (int i = 0; i < vn; i++) {
        struct wl_surface* s = vis[i];
        if (s->frame_pending) {
            s->frame_pending = 0;
            client_post(s->client, WL_EV_FRAME, 0, 0, 0);
        }
    }
}

void comp_init(void) {
    g_scrnx = io_get_scrnx();
    g_scrny = io_get_scrny();
    g_screen.pixels = io_get_vram();
    g_screen.pitch = g_scrnx;
    g_screen.w = g_scrnx;
    g_screen.h = g_scrny;
    g_screen.bytes = io_get_vram_bytes();
    lock_init(&g_comp_lock);
    shm_init();
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_surfaces, 0, sizeof(g_surfaces));
    g_inq_head = g_inq_tail = 0;
    g_damage_n = 0;
    g_damage_full = 1;
    g_cur_x = g_scrnx / 2;
    g_cur_y = g_scrny / 2;
    g_last_buttons = 0;
    g_session_active = 1;
}

static void drain_input(void) {
    while (g_inq_tail != g_inq_head) {
        struct gui_input ev;
        ev.type = g_inq[g_inq_tail].type;
        ev.a = g_inq[g_inq_tail].a;
        ev.b = g_inq[g_inq_tail].b;
        ev.c = g_inq[g_inq_tail].c;
        g_inq_tail = (g_inq_tail + 1) % GUI_INQ_SIZE;

        if (ev.type == GUI_IN_KEY) {
            wm_handle_key((uint8_t)ev.a, (int)ev.b, (uint8_t)ev.c);
        } else if (ev.type == GUI_IN_MOUSE) {
            int nx = g_cur_x + ev.a;
            int ny = g_cur_y + ev.b;
            if (nx < 0) nx = 0; if (nx >= g_scrnx) nx = g_scrnx - 1;
            if (ny < 0) ny = 0; if (ny >= g_scrny) ny = g_scrny - 1;
            if (nx != g_cur_x || ny != g_cur_y) {
                comp_damage_rect(g_cur_x, g_cur_y, CURSOR_W, CURSOR_H);
                comp_damage_rect(nx, ny, CURSOR_W, CURSOR_H);
                wm_handle_motion(nx, ny);
                g_cur_x = nx;
                g_cur_y = ny;
            }
            uint8_t btn = (uint8_t)ev.c;
            uint8_t edge = btn ^ g_last_buttons;
            if (edge) {
                wm_handle_button(g_cur_x, g_cur_y, btn, edge);
                g_last_buttons = btn;
            }
        }
    }
}

void comp_run(void) {
    while (g_session_active) {
        drain_input();
        if (wm_bar_check_dirty()) {
            comp_damage_rect(0, 0, g_scrnx, COMP_BAR_H);
        }
        if (g_damage_full || g_damage_n > 0) {
            repaint();
        }
        mtime_sleep(20);
    }
}
