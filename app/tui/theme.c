#include "theme.h"

typedef struct palette_entry {
    short fg;
    short bg;
    attr_t extra;
} palette_entry;

/* Flat colors from the standard xterm 256-color palette, no gradients. */
static const palette_entry dark_256[ROLE_COUNT] = {
    [ROLE_NORMAL] = {252, 234, A_NORMAL},    /* #d0d0d0 on #1c1c1c */
    [ROLE_DIM] = {243, 234, A_NORMAL},       /* #767676 */
    [ROLE_BAR] = {252, 236, A_NORMAL},       /* on #303030 */
    [ROLE_ACCENT] = {110, 234, A_BOLD},      /* #87afd7 */
    [ROLE_SELECTED] = {255, 24, A_BOLD},     /* #eeeeee on #005f87 */
    [ROLE_ZOMBIE] = {203, 234, A_BOLD},      /* #ff5f5f */
    [ROLE_DISK_WAIT] = {221, 234, A_BOLD},   /* #ffd75f */
};

static const palette_entry dark_8[ROLE_COUNT] = {
    [ROLE_NORMAL] = {COLOR_WHITE, COLOR_BLACK, A_NORMAL},
    [ROLE_DIM] = {COLOR_WHITE, COLOR_BLACK, A_DIM},
    [ROLE_BAR] = {COLOR_BLACK, COLOR_WHITE, A_NORMAL},
    [ROLE_ACCENT] = {COLOR_CYAN, COLOR_BLACK, A_BOLD},
    [ROLE_SELECTED] = {COLOR_WHITE, COLOR_BLUE, A_BOLD},
    [ROLE_ZOMBIE] = {COLOR_RED, COLOR_BLACK, A_BOLD},
    [ROLE_DISK_WAIT] = {COLOR_YELLOW, COLOR_BLACK, A_BOLD},
};

/* Roles that keep their own background even on the terminal's. */
static bool has_own_background(theme_role role)
{
    return role == ROLE_BAR || role == ROLE_SELECTED;
}

static attr_t attrs[ROLE_COUNT];

static void init_monochrome(void)
{
    for (int role = ROLE_NORMAL; role < ROLE_COUNT; role++) {
        attrs[role] = A_NORMAL;
    }
    attrs[ROLE_DIM] = A_DIM;
    attrs[ROLE_BAR] = A_REVERSE;
    attrs[ROLE_ACCENT] = A_BOLD;
    attrs[ROLE_SELECTED] = A_REVERSE | A_BOLD;
    attrs[ROLE_ZOMBIE] = A_BOLD;
    attrs[ROLE_DISK_WAIT] = A_BOLD;
}

void theme_init(bool own_background)
{
    if (!has_colors()) {
        init_monochrome();
        return;
    }

    start_color();
    if (!own_background) {
        use_default_colors();
    }

    const palette_entry *palette = own_background && COLORS >= 256 ? dark_256 : dark_8;

    for (int role = ROLE_NORMAL; role < ROLE_COUNT; role++) {
        const palette_entry *entry = &palette[role];
        short bg = own_background || has_own_background((theme_role)role) ? entry->bg : -1;

        init_pair((short)role, entry->fg, bg);
        attrs[role] = (attr_t)COLOR_PAIR(role) | entry->extra;
    }
}

attr_t theme_attr(theme_role role)
{
    return attrs[role];
}

void theme_use(theme_role role, attr_t extra)
{
    /* curses declares attrset() with an int parameter. */
    attrset((int)(attrs[role] | extra));
}
