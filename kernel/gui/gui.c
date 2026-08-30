#include "kernel/gui/gui.h"

#include "drivers/char/keyboard.h"
#include "drivers/char/mouse.h"
#include "kernel/asmFunc.h"
#include "drivers/char/console/io.h"
#include "kernel/init/pit/pit.h"
#include "kernel/gui/clients.h"
#include "kernel/gui/gfx.h"
#include "kernel/gui/server.h"
#include "kernel/gui/wm.h"

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
