#include "./gui.h"

#include "../device/keyboard.h"
#include "../device/mouse.h"
#include "../include/asmFunc.h"
#include "../initer/io/io.h"
#include "../initer/pit/pit.h"
#include "./clients.h"
#include "./gfx.h"
#include "./server.h"
#include "./wm.h"

static int running = 0;

int gui_session_run(void) {
    if (running)
        return -1;
    running = 1;

    asm_sti();

    io_set_gui_active(1);
    gfx_init_palette();
    comp_init();
    wm_init_state();

    keyboard_set_gui_hook(comp_post_key);
    mouse_set_hook(comp_post_mouse);

    comp_log("compositor: session started");
    clients_spawn_initial();

    comp_run();

    mtime_sleep(150);

    keyboard_set_gui_hook(0);
    mouse_set_hook(0);
    io_set_gui_active(0);
    io_clear_screen();

    running = 0;
    return 0;
}
