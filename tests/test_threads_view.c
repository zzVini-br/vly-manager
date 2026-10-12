#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test.h"
#include "threads_view.h"

#define FIXTURES VLY_FIXTURES_DIR "/proc"

/* Fixture process 100 (steam) has threads 100, 105 and 107. */
static vly_process fixture_steam(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    vly_snapshot_collect(&snap, FIXTURES);
    vly_process steam = snap.procs[vly_snapshot_index_of(&snap, 100)];
    vly_snapshot_free(&snap);
    return steam;
}

static void test_open_and_navigate(void)
{
    threads_view view;
    threads_view_init(&view);
    vly_process steam = fixture_steam();

    CHECK(threads_view_open(&view, &steam, FIXTURES) == 0);
    CHECK(view.open);
    CHECK(view.cur->count == 3);
    CHECK(view.cursor == 0);

    threads_view_move(&view, 5);
    CHECK(view.cursor == 2);
    threads_view_scroll_to_cursor(&view, 2);
    CHECK(view.scroll == 1);
    threads_view_move(&view, -10);
    CHECK(view.cursor == 0);
    threads_view_scroll_to_cursor(&view, 2);
    CHECK(view.scroll == 0);

    CHECK(threads_view_refresh(&view, FIXTURES, 100, 4) == 0);
    CHECK(view.cur->count == 3);

    threads_view_close(&view);
    CHECK(!view.open);
    threads_view_free(&view);
}

static void test_refuses_other_process(void)
{
    threads_view view;
    threads_view_init(&view);
    vly_process steam = fixture_steam();

    /* Same pid, different start time: the pid was reused. */
    steam.start_time++;
    CHECK(threads_view_open(&view, &steam, FIXTURES) == -ESRCH);
    CHECK(!view.open);

    steam.pid = 4242;
    CHECK(threads_view_open(&view, &steam, FIXTURES) == -ESRCH);

    threads_view_free(&view);
}

static void test_process_exits_while_shown(void)
{
    pid_t child = fork();
    if (child == 0) {
        for (;;) {
            pause();
        }
    }

    vly_snapshot snap;
    vly_snapshot_init(&snap);
    int32_t index = VLY_NO_INDEX;
    for (int tries = 0; tries < 100 && index == VLY_NO_INDEX; tries++) {
        vly_snapshot_collect(&snap, "/proc");
        index = vly_snapshot_index_of(&snap, child);
    }

    threads_view view;
    threads_view_init(&view);
    CHECK(index != VLY_NO_INDEX);
    if (index != VLY_NO_INDEX) {
        CHECK(threads_view_open(&view, &snap.procs[index], "/proc") == 0);
        CHECK(view.cur->count == 1);
    }

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);

    /* Gone: refresh says so and the last threads stay on screen. */
    CHECK(threads_view_refresh(&view, "/proc", 100, 4) == -ESRCH);
    CHECK(view.cur->count == 1);

    threads_view_free(&view);
    vly_snapshot_free(&snap);
}

int main(void)
{
    test_open_and_navigate();
    test_refuses_other_process();
    test_process_exits_while_shown();
    return TEST_RESULT();
}
