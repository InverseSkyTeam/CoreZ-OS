#ifndef GUI_LAYOUT_H
#define GUI_LAYOUT_H

#include "./gfx.h"

enum layout_kind {
    LAYOUT_MASTER_STACK = 0,
    LAYOUT_TALL,
    LAYOUT_WIDE,
    LAYOUT_MONOCLE,
    LAYOUT_COUNT
};

struct layout_params {
    enum layout_kind kind;
    int mfact;
    int gap;
};

void layout_arrange(const struct layout_params *p, int n, struct gfx_rect area,
                    struct gfx_rect *out);

const char *layout_name(enum layout_kind kind);

#endif
