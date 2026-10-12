#ifndef VLY_TREE_VIEW_H
#define VLY_TREE_VIEW_H

/*
 * The process tree as a scrollable list of rows: which processes are
 * visible (collapsed subtrees and, optionally, kernel threads are hidden),
 * how deep each one is, and which one is selected. It knows nothing about
 * curses, so the navigation logic can be tested on its own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "vly/snapshot.h"

/* Identifies a process across snapshots; the start time tells a reused pid
 * apart from the process that had it before. */
typedef struct proc_key {
    pid_t pid;
    unsigned long long start_time;
} proc_key;

typedef struct tree_row {
    int32_t index; /* into the snapshot */
    int depth;     /* 0 for top-level processes */
} tree_row;

typedef struct tree_view {
    bool show_kernel;

    const vly_snapshot *snap; /* the snapshot the rows refer to */
    tree_row *rows;
    size_t row_count;
    size_t row_capacity;

    proc_key *collapsed;
    size_t collapsed_count;
    size_t collapsed_capacity;

    size_t cursor;      /* selected row, valid when row_count > 0 */
    proc_key selection; /* the selected process, kept across rebuilds */
    size_t scroll;      /* first row on screen */
} tree_view;

void tree_view_init(tree_view *view);
void tree_view_free(tree_view *view);

/*
 * Rebuilds the rows from `snap` (whose tree must be built) and keeps the
 * same process selected; if it is gone, the cursor stays at the same
 * position. `snap` must outlive the rows. Returns 0 or -ENOMEM.
 */
int tree_view_rebuild(tree_view *view, const vly_snapshot *snap);

/* The selected process, or NULL when there are no rows. */
const vly_process *tree_view_selected(const tree_view *view);

/* Moves the cursor by `delta` rows, clamped to the list. */
void tree_view_move(tree_view *view, long delta);

/* Moves the cursor to the first or the last row. */
void tree_view_move_to_start(tree_view *view);
void tree_view_move_to_end(tree_view *view);

/* Left arrow: collapses the selected subtree, or selects the parent if it
 * is already collapsed or has no children. Returns 0 or -ENOMEM. */
int tree_view_left(tree_view *view);

/* Right arrow: expands the selected subtree, or selects its first child if
 * it is already expanded. Returns 0 or -ENOMEM. */
int tree_view_right(tree_view *view);

/*
 * Selects the first row, scanning down from the cursor (or the row after
 * it, with `from_next`) and wrapping around, whose name starts with
 * `prefix`, ignoring case. Returns false, leaving the cursor alone, if no
 * visible row matches.
 */
bool tree_view_select_prefix(tree_view *view, const char *prefix, bool from_next);

/* Scrolls so the cursor is within a window of `height` rows. */
void tree_view_scroll_to_cursor(tree_view *view, size_t height);

bool tree_view_has_children(const tree_view *view, int32_t index);
bool tree_view_is_collapsed(const tree_view *view, int32_t index);

/* Number of processes hidden under a collapsed process. */
size_t tree_view_hidden_below(const tree_view *view, int32_t index);

/*
 * Writes the tree lines drawn before the name of `row` into `buf`, e.g.
 * "│  ├▾ " (UTF-8): one 3-column segment per ancestor level, then the
 * branch, with ▾ / ▸ marking expanded / collapsed parents.
 */
void tree_view_prefix(const tree_view *view, const tree_row *row, char *buf, size_t size);

#endif /* VLY_TREE_VIEW_H */
