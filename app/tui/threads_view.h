#ifndef VLY_THREADS_VIEW_H
#define VLY_THREADS_VIEW_H

/*
 * The threads of one process as a scrollable list, refreshed on demand.
 * Like tree_view, it knows nothing about curses.
 */

#include <stdbool.h>
#include <stddef.h>

#include "tree_view.h"
#include "vly/snapshot.h"

typedef struct threads_view {
    bool open;
    proc_key owner; /* the process whose threads are shown */
    char owner_name[VLY_NAME_SIZE];

    vly_snapshot snapshots[2];
    vly_snapshot *cur; /* entries are threads: `pid` holds the thread id */
    vly_snapshot *prev;

    size_t cursor;
    size_t scroll;
} threads_view;

void threads_view_init(threads_view *view);
void threads_view_free(threads_view *view);

/* Starts showing the threads of `proc`, read from `proc_root`. Returns 0 or
 * a negative errno value (-ESRCH if the process is gone). */
int threads_view_open(threads_view *view, const vly_process *proc, const char *proc_root);

/* Stops showing threads; the buffers are kept for the next open. */
void threads_view_close(threads_view *view);

/*
 * Reads the threads again and computes their CPU usage since the last
 * read. Returns 0, or -ESRCH once the process has exited (or its pid now
 * belongs to another process); the view then keeps its last threads.
 */
int threads_view_refresh(threads_view *view, const char *proc_root, long ticks_per_second,
                         long cpu_count);

/* Moves the cursor by `delta` rows, clamped to the list. */
void threads_view_move(threads_view *view, long delta);

/* Scrolls so the cursor is within a window of `height` rows. */
void threads_view_scroll_to_cursor(threads_view *view, size_t height);

#endif /* VLY_THREADS_VIEW_H */
