#include "threads_view.h"

#include <errno.h>
#include <string.h>

void threads_view_init(threads_view *view)
{
    memset(view, 0, sizeof(*view));
    vly_snapshot_init(&view->snapshots[0]);
    vly_snapshot_init(&view->snapshots[1]);
    view->cur = &view->snapshots[0];
    view->prev = &view->snapshots[1];
}

void threads_view_free(threads_view *view)
{
    vly_snapshot_free(&view->snapshots[0]);
    vly_snapshot_free(&view->snapshots[1]);
    threads_view_init(view);
}

/*
 * Collects the owner's threads into `snap`. The main thread's id is the
 * process id and it shares the process's start time, so a different start
 * time means the pid now belongs to another process.
 */
static int collect(const threads_view *view, vly_snapshot *snap, const char *proc_root)
{
    int err = vly_snapshot_collect_threads(snap, proc_root, view->owner.pid);
    if (err == -ENOENT) {
        return -ESRCH;
    }
    if (err != 0) {
        return err;
    }

    int32_t main_thread = vly_snapshot_index_of(snap, view->owner.pid);
    if (main_thread == VLY_NO_INDEX ||
        snap->procs[main_thread].start_time != view->owner.start_time) {
        return -ESRCH;
    }
    return 0;
}

int threads_view_open(threads_view *view, const vly_process *proc, const char *proc_root)
{
    view->owner = (proc_key){.pid = proc->pid, .start_time = proc->start_time};
    memcpy(view->owner_name, proc->name, sizeof(view->owner_name));
    view->cursor = 0;
    view->scroll = 0;
    view->prev->count = 0;

    int err = collect(view, view->cur, proc_root);
    view->open = err == 0;
    return err;
}

void threads_view_close(threads_view *view)
{
    view->open = false;
}

int threads_view_refresh(threads_view *view, const char *proc_root, long ticks_per_second,
                         long cpu_count)
{
    /* Collect into the spare buffer; the current one stays valid on error. */
    vly_snapshot *next = view->prev;
    int err = collect(view, next, proc_root);
    if (err != 0) {
        return err;
    }

    vly_snapshot_compute_cpu(next, view->cur, ticks_per_second, cpu_count);
    view->prev = view->cur;
    view->cur = next;

    if (view->cur->count > 0 && view->cursor >= view->cur->count) {
        view->cursor = view->cur->count - 1;
    }
    return 0;
}

void threads_view_move(threads_view *view, long delta)
{
    size_t count = view->cur->count;
    if (count == 0) {
        return;
    }

    long target = (long)view->cursor + delta;
    if (target < 0) {
        target = 0;
    } else if ((size_t)target >= count) {
        target = (long)count - 1;
    }
    view->cursor = (size_t)target;
}

void threads_view_scroll_to_cursor(threads_view *view, size_t height)
{
    if (height == 0) {
        return;
    }

    if (view->cursor < view->scroll) {
        view->scroll = view->cursor;
    } else if (view->cursor >= view->scroll + height) {
        view->scroll = view->cursor - height + 1;
    }

    size_t count = view->cur->count;
    size_t max_scroll = count > height ? count - height : 0;
    if (view->scroll > max_scroll) {
        view->scroll = max_scroll;
    }
}
