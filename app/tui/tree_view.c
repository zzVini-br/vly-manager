#include "tree_view.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "match.h"
/* Tree drawing pieces, each 3 columns wide once a marker follows. */
#define LINE_CONTINUES "\xe2\x94\x82  " /* "│  " */
#define LINE_BLANK "   "
#define BRANCH_MIDDLE "\xe2\x94\x9c"    /* "├" */
#define BRANCH_LAST "\xe2\x94\x94"      /* "└" */
#define MARK_LEAF "\xe2\x94\x80"        /* "─" */
#define MARK_EXPANDED "\xe2\x96\xbe"    /* "▾" */
#define MARK_COLLAPSED "\xe2\x96\xb8"   /* "▸" */

/* Deeper ancestor levels are not drawn; real trees stay far below this. */
#define MAX_DRAWN_LEVELS 64

void tree_view_init(tree_view *view)
{
    memset(view, 0, sizeof(*view));
}

void tree_view_free(tree_view *view)
{
    free(view->rows);
    free(view->collapsed);
    tree_view_init(view);
}

static const vly_process *proc_at(const tree_view *view, int32_t index)
{
    return &view->snap->procs[index];
}

static bool is_visible(const tree_view *view, int32_t index)
{
    return view->show_kernel || !proc_at(view, index)->is_kernel_thread;
}

static int32_t next_visible(const tree_view *view, int32_t index)
{
    while (index != VLY_NO_INDEX && !is_visible(view, index)) {
        index = proc_at(view, index)->next_sibling;
    }
    return index;
}

static proc_key key_of(const vly_process *proc)
{
    return (proc_key){.pid = proc->pid, .start_time = proc->start_time};
}

static bool key_matches(proc_key key, const vly_process *proc)
{
    return key.pid == proc->pid && key.start_time == proc->start_time;
}

static bool find_collapsed(const tree_view *view, const vly_process *proc, size_t *position)
{
    for (size_t i = 0; i < view->collapsed_count; i++) {
        if (key_matches(view->collapsed[i], proc)) {
            if (position != NULL) {
                *position = i;
            }
            return true;
        }
    }
    return false;
}

bool tree_view_is_collapsed(const tree_view *view, int32_t index)
{
    return find_collapsed(view, proc_at(view, index), NULL);
}

bool tree_view_has_children(const tree_view *view, int32_t index)
{
    return next_visible(view, proc_at(view, index)->first_child) != VLY_NO_INDEX;
}

static size_t count_below(const tree_view *view, int32_t index, size_t depth_left)
{
    if (depth_left == 0) {
        return 0;
    }

    size_t total = 0;
    for (int32_t c = next_visible(view, proc_at(view, index)->first_child); c != VLY_NO_INDEX;
         c = next_visible(view, proc_at(view, c)->next_sibling)) {
        total += 1 + count_below(view, c, depth_left - 1);
    }
    return total;
}

size_t tree_view_hidden_below(const tree_view *view, int32_t index)
{
    return count_below(view, index, view->snap->count);
}

/* Appends the visible subtree of `index`. Depth is bounded by the number of
 * processes, so a pid-reuse cycle cannot recurse forever. */
static void add_subtree(tree_view *view, int32_t index, int depth)
{
    view->rows[view->row_count++] = (tree_row){.index = index, .depth = depth};

    if ((size_t)depth >= view->snap->count || tree_view_is_collapsed(view, index)) {
        return;
    }

    for (int32_t c = next_visible(view, proc_at(view, index)->first_child);
         c != VLY_NO_INDEX && view->row_count < view->row_capacity;
         c = next_visible(view, proc_at(view, c)->next_sibling)) {
        add_subtree(view, c, depth + 1);
    }
}

/* Filter mode: each matching process is shown at the top level with its
 * subtree; the search does not descend into a subtree already shown. */
static void add_matches(tree_view *view, int32_t first, size_t depth_left)
{
    if (depth_left == 0) {
        return;
    }

    for (int32_t i = next_visible(view, first);
         i != VLY_NO_INDEX && view->row_count < view->row_capacity;
         i = next_visible(view, proc_at(view, i)->next_sibling)) {
        if (match_name_contains(proc_at(view, i)->name, view->filter)) {
            add_subtree(view, i, 0);
        } else {
            add_matches(view, proc_at(view, i)->first_child, depth_left - 1);
        }
    }
}

/* Forgets collapsed processes that no longer exist. */
static void prune_collapsed(tree_view *view)
{
    size_t kept = 0;
    for (size_t i = 0; i < view->collapsed_count; i++) {
        int32_t index = vly_snapshot_index_of(view->snap, view->collapsed[i].pid);
        if (index != VLY_NO_INDEX && key_matches(view->collapsed[i], proc_at(view, index))) {
            view->collapsed[kept++] = view->collapsed[i];
        }
    }
    view->collapsed_count = kept;
}

static void select_row(tree_view *view, size_t row)
{
    view->cursor = row;
    view->selection = key_of(proc_at(view, view->rows[row].index));
}

int tree_view_rebuild(tree_view *view, const vly_snapshot *snap)
{
    view->snap = snap;
    view->row_count = 0;

    /* Every process appears at most once, so the snapshot size bounds the rows. */
    if (view->row_capacity < snap->count) {
        tree_row *rows = realloc(view->rows, snap->count * sizeof(*rows));
        if (rows == NULL) {
            return -ENOMEM;
        }
        view->rows = rows;
        view->row_capacity = snap->count;
    }

    prune_collapsed(view);

    if (view->filter[0] != '\0') {
        add_matches(view, snap->first_root, snap->count);
    } else {
        for (int32_t root = next_visible(view, snap->first_root);
             root != VLY_NO_INDEX && view->row_count < view->row_capacity;
             root = next_visible(view, proc_at(view, root)->next_sibling)) {
            add_subtree(view, root, 0);
        }
    }

    if (view->row_count == 0) {
        view->cursor = 0;
        view->scroll = 0;
        return 0;
    }

    /* Follow the selected process; if it is gone, stay at the same row. */
    for (size_t i = 0; i < view->row_count; i++) {
        if (key_matches(view->selection, proc_at(view, view->rows[i].index))) {
            view->cursor = i;
            return 0;
        }
    }
    select_row(view, view->cursor < view->row_count ? view->cursor : view->row_count - 1);
    return 0;
}

int tree_view_set_filter(tree_view *view, const char *text)
{
    snprintf(view->filter, sizeof(view->filter), "%s", text);
    if (view->snap == NULL) {
        return 0; /* applied by the first rebuild */
    }

    int err = tree_view_rebuild(view, view->snap);
    if (err != 0 || view->row_count == 0) {
        return err;
    }

    /* Rebuild keeps the cursor's position when the selection disappears;
     * for a new filter the first match is the better place. */
    if (!key_matches(view->selection, proc_at(view, view->rows[view->cursor].index))) {
        select_row(view, 0);
    }
    view->scroll = 0;
    return 0;
}

const vly_process *tree_view_selected(const tree_view *view)
{
    return view->row_count == 0 ? NULL : proc_at(view, view->rows[view->cursor].index);
}

void tree_view_move(tree_view *view, long delta)
{
    if (view->row_count == 0) {
        return;
    }

    long target = (long)view->cursor + delta;
    if (target < 0) {
        target = 0;
    } else if ((size_t)target >= view->row_count) {
        target = (long)view->row_count - 1;
    }
    select_row(view, (size_t)target);
}

void tree_view_move_to_start(tree_view *view)
{
    if (view->row_count > 0) {
        select_row(view, 0);
    }
}

void tree_view_move_to_end(tree_view *view)
{
    if (view->row_count > 0) {
        select_row(view, view->row_count - 1);
    }
}

static int set_collapsed(tree_view *view, int32_t index, bool collapsed)
{
    const vly_process *proc = proc_at(view, index);
    size_t position;
    bool is_collapsed = find_collapsed(view, proc, &position);

    if (collapsed == is_collapsed) {
        return 0;
    }

    if (collapsed) {
        if (view->collapsed_count == view->collapsed_capacity) {
            size_t capacity = view->collapsed_capacity == 0 ? 16 : view->collapsed_capacity * 2;
            proc_key *keys = realloc(view->collapsed, capacity * sizeof(*keys));
            if (keys == NULL) {
                return -ENOMEM;
            }
            view->collapsed = keys;
            view->collapsed_capacity = capacity;
        }
        view->collapsed[view->collapsed_count++] = key_of(proc);
    } else {
        view->collapsed[position] = view->collapsed[--view->collapsed_count];
    }

    return tree_view_rebuild(view, view->snap);
}

int tree_view_left(tree_view *view)
{
    if (view->row_count == 0) {
        return 0;
    }

    int32_t index = view->rows[view->cursor].index;
    if (tree_view_has_children(view, index) && !tree_view_is_collapsed(view, index)) {
        return set_collapsed(view, index, true);
    }

    /* The parent is the closest shallower row above. */
    int depth = view->rows[view->cursor].depth;
    for (size_t i = view->cursor; i-- > 0;) {
        if (view->rows[i].depth < depth) {
            select_row(view, i);
            break;
        }
    }
    return 0;
}

int tree_view_right(tree_view *view)
{
    if (view->row_count == 0) {
        return 0;
    }

    int32_t index = view->rows[view->cursor].index;
    if (!tree_view_has_children(view, index)) {
        return 0;
    }
    if (tree_view_is_collapsed(view, index)) {
        return set_collapsed(view, index, false);
    }

    /* Expanded: the first child is the next row. */
    tree_view_move(view, 1);
    return 0;
}

bool tree_view_select_prefix(tree_view *view, const char *prefix, bool from_next)
{
    size_t len = strlen(prefix);
    size_t count = view->row_count;
    if (count == 0 || len == 0) {
        return false;
    }

    size_t start = from_next ? view->cursor + 1 : view->cursor;
    for (size_t step = 0; step < count; step++) {
        size_t row = (start + step) % count;
        if (strncasecmp(proc_at(view, view->rows[row].index)->name, prefix, len) == 0) {
            select_row(view, row);
            return true;
        }
    }
    return false;
}

void tree_view_scroll_to_cursor(tree_view *view, size_t height)
{
    if (height == 0) {
        return;
    }

    if (view->cursor < view->scroll) {
        view->scroll = view->cursor;
    } else if (view->cursor >= view->scroll + height) {
        view->scroll = view->cursor - height + 1;
    }

    /* Don't leave empty space below the last row when the list shrinks. */
    size_t max_scroll = view->row_count > height ? view->row_count - height : 0;
    if (view->scroll > max_scroll) {
        view->scroll = max_scroll;
    }
}

static void append(char *buf, size_t size, size_t *len, const char *text)
{
    size_t text_len = strlen(text);
    if (*len + text_len < size) {
        memcpy(buf + *len, text, text_len + 1);
        *len += text_len;
    }
}

void tree_view_prefix(const tree_view *view, const tree_row *row, char *buf, size_t size)
{
    size_t len = 0;
    int32_t index = row->index;
    const char *mark = !tree_view_has_children(view, index) ? MARK_LEAF
                       : tree_view_is_collapsed(view, index) ? MARK_COLLAPSED
                                                             : MARK_EXPANDED;

    if (size == 0) {
        return;
    }
    buf[0] = '\0';

    if (row->depth == 0) {
        /* Top-level processes get only the expand marker, if any. */
        if (tree_view_has_children(view, index)) {
            append(buf, size, &len, mark);
            append(buf, size, &len, " ");
        }
        return;
    }

    /* Ancestors from depth 1 down to the parent decide the vertical lines:
     * a line continues where that ancestor has more siblings below it. */
    int32_t ancestors[MAX_DRAWN_LEVELS];
    int levels = row->depth - 1;
    for (int level = 0; level < MAX_DRAWN_LEVELS; level++) {
        ancestors[level] = VLY_NO_INDEX;
    }
    int32_t p = proc_at(view, index)->parent;
    for (int level = levels; level >= 1 && p != VLY_NO_INDEX; level--) {
        if (level <= MAX_DRAWN_LEVELS) {
            ancestors[level - 1] = p;
        }
        p = proc_at(view, p)->parent;
    }

    for (int level = 1; level <= levels && level <= MAX_DRAWN_LEVELS; level++) {
        int32_t ancestor = ancestors[level - 1];
        bool continues = ancestor != VLY_NO_INDEX &&
                         next_visible(view, proc_at(view, ancestor)->next_sibling) != VLY_NO_INDEX;
        append(buf, size, &len, continues ? LINE_CONTINUES : LINE_BLANK);
    }

    bool last = next_visible(view, proc_at(view, index)->next_sibling) == VLY_NO_INDEX;
    append(buf, size, &len, last ? BRANCH_LAST : BRANCH_MIDDLE);
    append(buf, size, &len, mark);
    append(buf, size, &len, " ");
}
