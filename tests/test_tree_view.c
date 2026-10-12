#include <string.h>

#include "test.h"
#include "tree_view.h"

/*
 * Fixture tree:
 *   systemd (1)
 *   └─ steam (100)
 *      ├─ "Web Content) (x" (101, zombie)
 *      └─ reaper (1000)
 *   kthreadd (2)       kernel thread, hidden by default
 *   └─ kworker (50)    kernel thread
 */

static void load_fixtures(vly_snapshot *snap)
{
    vly_snapshot_init(snap);
    CHECK(vly_snapshot_collect(snap, VLY_FIXTURES_DIR "/proc") == 0);
    vly_snapshot_build_tree(snap);
}

static pid_t row_pid(const tree_view *view, size_t row)
{
    return view->snap->procs[view->rows[row].index].pid;
}

static pid_t selected_pid(const tree_view *view)
{
    const vly_process *proc = tree_view_selected(view);
    return proc == NULL ? -1 : proc->pid;
}

static bool prefix_is(const tree_view *view, size_t row, const char *expected)
{
    char buf[256];
    tree_view_prefix(view, &view->rows[row], buf, sizeof(buf));
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "  row %zu prefix: \"%s\", expected \"%s\"\n", row, buf, expected);
        return false;
    }
    return true;
}

/* Removes a process from the snapshot, as if it had exited. */
static void remove_pid(vly_snapshot *snap, pid_t pid)
{
    int32_t index = vly_snapshot_index_of(snap, pid);
    if (index == VLY_NO_INDEX) {
        return;
    }
    memmove(&snap->procs[index], &snap->procs[index + 1],
            (snap->count - (size_t)index - 1) * sizeof(*snap->procs));
    snap->count--;
    vly_snapshot_build_tree(snap);
}

static void test_rows_and_prefixes(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);

    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 4);
    if (view.row_count == 4) {
        CHECK(row_pid(&view, 0) == 1 && view.rows[0].depth == 0);
        CHECK(row_pid(&view, 1) == 100 && view.rows[1].depth == 1);
        CHECK(row_pid(&view, 2) == 101 && view.rows[2].depth == 2);
        CHECK(row_pid(&view, 3) == 1000 && view.rows[3].depth == 2);

        CHECK(prefix_is(&view, 0, "▾ "));
        CHECK(prefix_is(&view, 1, "└▾ "));
        CHECK(prefix_is(&view, 2, "   ├─ "));
        CHECK(prefix_is(&view, 3, "   └─ "));
    }
    CHECK(selected_pid(&view) == 1);

    /* Kernel threads appear on request. */
    view.show_kernel = true;
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 6);
    if (view.row_count == 6) {
        CHECK(row_pid(&view, 4) == 2);
        CHECK(row_pid(&view, 5) == 50);
        CHECK(prefix_is(&view, 5, "└─ "));
    }

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_navigation(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);

    tree_view_move(&view, 1);
    CHECK(selected_pid(&view) == 100);

    /* Right on an expanded parent goes to its first child... */
    CHECK(tree_view_right(&view) == 0);
    CHECK(selected_pid(&view) == 101);
    /* ...right on a leaf does nothing, left on a leaf goes to the parent. */
    CHECK(tree_view_right(&view) == 0);
    CHECK(selected_pid(&view) == 101);
    CHECK(tree_view_left(&view) == 0);
    CHECK(selected_pid(&view) == 100);

    /* Left on an expanded parent collapses it and keeps it selected. */
    CHECK(tree_view_left(&view) == 0);
    CHECK(view.row_count == 2);
    CHECK(selected_pid(&view) == 100);
    CHECK(tree_view_is_collapsed(&view, view.rows[1].index));
    CHECK(tree_view_hidden_below(&view, view.rows[1].index) == 2);
    CHECK(prefix_is(&view, 1, "└▸ "));

    /* Right expands it again. */
    CHECK(tree_view_right(&view) == 0);
    CHECK(view.row_count == 4);
    CHECK(selected_pid(&view) == 100);

    /* Moves are clamped to the list. */
    tree_view_move(&view, -10);
    CHECK(selected_pid(&view) == 1);
    tree_view_move(&view, 10);
    CHECK(selected_pid(&view) == 1000);
    tree_view_move_to_start(&view);
    CHECK(selected_pid(&view) == 1);
    tree_view_move_to_end(&view);
    CHECK(selected_pid(&view) == 1000);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_selection_follows_process(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);

    tree_view_move_to_end(&view);
    CHECK(selected_pid(&view) == 1000);

    /* Another process exits: the selection stays on reaper, one row up. */
    remove_pid(&snap, 101);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 3);
    CHECK(view.cursor == 2);
    CHECK(selected_pid(&view) == 1000);

    /* The selected process exits: the cursor stays in place, clamped. */
    remove_pid(&snap, 1000);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 2);
    CHECK(selected_pid(&view) == 100);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_collapsed_state_survives_and_is_pruned(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);

    tree_view_move(&view, 1);
    CHECK(tree_view_left(&view) == 0); /* collapse steam */
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 2);
    CHECK(view.collapsed_count == 1);

    /* Once steam is gone, its collapsed state is forgotten. */
    remove_pid(&snap, 100);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.collapsed_count == 0);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_scrolling(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);

    tree_view_move_to_end(&view);
    tree_view_scroll_to_cursor(&view, 2);
    CHECK(view.scroll == 2);

    tree_view_move_to_start(&view);
    tree_view_scroll_to_cursor(&view, 2);
    CHECK(view.scroll == 0);

    /* A window taller than the list never scrolls. */
    tree_view_move_to_end(&view);
    tree_view_scroll_to_cursor(&view, 10);
    CHECK(view.scroll == 0);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_select_by_prefix(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(selected_pid(&view) == 1); /* systemd */

    /* From the next row, wrapping around: steam, then back to systemd. */
    CHECK(tree_view_select_prefix(&view, "s", true));
    CHECK(selected_pid(&view) == 100);
    CHECK(tree_view_select_prefix(&view, "s", true));
    CHECK(selected_pid(&view) == 1);

    /* From the current row, a match there is kept. */
    CHECK(tree_view_select_prefix(&view, "sy", false));
    CHECK(selected_pid(&view) == 1);

    /* Case-insensitive, and names with spaces work. */
    CHECK(tree_view_select_prefix(&view, "WEB co", false));
    CHECK(selected_pid(&view) == 101);
    CHECK(tree_view_select_prefix(&view, "re", true));
    CHECK(selected_pid(&view) == 1000);

    /* No match: the cursor stays. */
    CHECK(!tree_view_select_prefix(&view, "zzz", true));
    CHECK(selected_pid(&view) == 1000);

    /* Hidden rows are not searched: kthreadd is a kernel thread. */
    CHECK(!tree_view_select_prefix(&view, "kthread", true));

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_filter(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);
    tree_view view;
    tree_view_init(&view);

    /* Set before the first rebuild: applied once there is a snapshot. */
    CHECK(tree_view_set_filter(&view, "STEAM") == 0);
    CHECK(tree_view_rebuild(&view, &snap) == 0);

    /* steam comes up to the top level with its whole subtree. */
    CHECK(view.row_count == 3);
    if (view.row_count == 3) {
        CHECK(row_pid(&view, 0) == 100 && view.rows[0].depth == 0);
        CHECK(row_pid(&view, 1) == 101 && view.rows[1].depth == 1);
        CHECK(row_pid(&view, 2) == 1000 && view.rows[2].depth == 1);
        CHECK(prefix_is(&view, 0, "▾ "));
        CHECK(prefix_is(&view, 1, "├─ "));
        CHECK(prefix_is(&view, 2, "└─ "));
    }
    CHECK(selected_pid(&view) == 100);

    /* A match inside an already shown subtree is not listed twice. */
    CHECK(tree_view_set_filter(&view, "s") == 0);
    CHECK(view.row_count == 4);

    /* The selection stays while still visible... */
    tree_view_move_to_end(&view);
    CHECK(selected_pid(&view) == 1000);
    CHECK(tree_view_set_filter(&view, "steam") == 0);
    CHECK(selected_pid(&view) == 1000);
    /* ...and moves to the first match otherwise. */
    CHECK(tree_view_set_filter(&view, "web") == 0);
    CHECK(view.row_count == 1);
    CHECK(selected_pid(&view) == 101);

    /* No match: no rows and no selection. */
    CHECK(tree_view_set_filter(&view, "zzz") == 0);
    CHECK(view.row_count == 0);
    CHECK(tree_view_selected(&view) == NULL);

    /* Kernel threads stay hidden while filtering. */
    CHECK(tree_view_set_filter(&view, "kthread") == 0);
    CHECK(view.row_count == 0);

    /* Clearing the filter brings the full tree back. */
    CHECK(tree_view_set_filter(&view, "") == 0);
    CHECK(view.row_count == 4);
    CHECK(row_pid(&view, 0) == 1);

    /* Filtering survives refreshes. */
    CHECK(tree_view_set_filter(&view, "reaper") == 0);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    CHECK(view.row_count == 1 && selected_pid(&view) == 1000);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

static void test_sorted_modes(void)
{
    vly_snapshot snap;
    load_fixtures(&snap);

    /* CPU usage comes from two snapshots; set it directly here. Resident
     * pages in the fixtures: systemd 3000, steam 52000, reaper 128, 101 0. */
    snap.procs[vly_snapshot_index_of(&snap, 1)].cpu_percent = 10.0;
    snap.procs[vly_snapshot_index_of(&snap, 100)].cpu_percent = 30.0;
    snap.procs[vly_snapshot_index_of(&snap, 1000)].cpu_percent = 50.0;

    tree_view view;
    tree_view_init(&view);
    CHECK(tree_view_rebuild(&view, &snap) == 0);
    tree_view_move_to_end(&view); /* reaper */

    CHECK(tree_view_set_mode(&view, TREE_VIEW_BY_CPU) == 0);
    CHECK(view.row_count == 4);
    if (view.row_count == 4) {
        CHECK(row_pid(&view, 0) == 1000);
        CHECK(row_pid(&view, 1) == 100);
        CHECK(row_pid(&view, 2) == 1);
        CHECK(row_pid(&view, 3) == 101);
        CHECK(view.rows[1].depth == 0);
        CHECK(prefix_is(&view, 1, ""));
    }
    CHECK(selected_pid(&view) == 1000); /* the selection follows the process */

    CHECK(tree_view_set_mode(&view, TREE_VIEW_BY_MEMORY) == 0);
    if (view.row_count == 4) {
        CHECK(row_pid(&view, 0) == 100);
        CHECK(row_pid(&view, 1) == 1);
        CHECK(row_pid(&view, 2) == 1000);
        CHECK(row_pid(&view, 3) == 101);
    }

    /* Nothing to fold in a flat list. */
    tree_view_move_to_start(&view);
    CHECK(tree_view_left(&view) == 0);
    CHECK(tree_view_right(&view) == 0);
    CHECK(view.row_count == 4 && view.collapsed_count == 0);

    /* The filter keeps matching processes only, without their subtrees. */
    CHECK(tree_view_set_filter(&view, "s") == 0);
    CHECK(view.row_count == 2);
    CHECK(row_pid(&view, 0) == 100 && row_pid(&view, 1) == 1);
    CHECK(tree_view_set_filter(&view, "") == 0);

    /* Back to the tree. */
    CHECK(tree_view_set_mode(&view, TREE_VIEW_TREE) == 0);
    CHECK(row_pid(&view, 0) == 1 && view.rows[1].depth == 1);

    tree_view_free(&view);
    vly_snapshot_free(&snap);
}

int main(void)
{
    test_sorted_modes();
    test_filter();
    test_select_by_prefix();
    test_rows_and_prefixes();
    test_navigation();
    test_selection_follows_process();
    test_collapsed_state_survives_and_is_pruned();
    test_scrolling();
    return TEST_RESULT();
}
