#ifndef VLY_TUI_THEME_H
#define VLY_TUI_THEME_H

#include <curses.h>
#include <stdbool.h>

/* What a piece of the screen is, independent of how it is colored. */
typedef enum theme_role {
    ROLE_NORMAL = 1, /* color pair 0 is reserved by curses */
    ROLE_DIM,
    ROLE_BAR, /* header and footer */
    ROLE_ACCENT,
    ROLE_SELECTED,
    ROLE_ZOMBIE,
    ROLE_DISK_WAIT,
    ROLE_COUNT
} theme_role;

/*
 * Sets up the colors; call after initscr(). With `own_background`, vly
 * paints its own dark palette (256 colors, or the 8 basic ones on simpler
 * terminals); otherwise it keeps the terminal's background and colors
 * only the highlights. Without color support, attributes such as reverse
 * video stand in.
 */
void theme_init(bool own_background);

/* Attributes (color pair plus bold, reverse...) for a role. */
attr_t theme_attr(theme_role role);

/* Makes `role`, plus any `extra` attributes, the current drawing style. */
void theme_use(theme_role role, attr_t extra);

#endif /* VLY_TUI_THEME_H */
