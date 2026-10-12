#ifndef VLY_TUI_H
#define VLY_TUI_H

#include <stdbool.h>

typedef struct tui_options {
    bool own_background; /* paint vly's dark palette instead of the terminal's */
} tui_options;

/* Runs the interactive terminal UI until the user quits. Returns the
 * process exit status. */
int tui_run(const tui_options *opts);

#endif /* VLY_TUI_H */
