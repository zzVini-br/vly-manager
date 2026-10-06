#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "vly/snapshot.h"

static void test_collect_fixtures(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    CHECK(vly_snapshot_collect(&snap, VLY_FIXTURES_DIR "/proc") == 0);

    /* 999 has no stat file and "uptime" is not a pid: both are skipped. */
    CHECK(snap.count == 6);
    for (size_t i = 1; i < snap.count; i++) {
        CHECK(snap.procs[i - 1].pid < snap.procs[i].pid);
    }

    int32_t steam = vly_snapshot_index_of(&snap, 100);
    CHECK(steam != VLY_NO_INDEX);
    if (steam != VLY_NO_INDEX) {
        CHECK(strcmp(snap.procs[steam].name, "steam") == 0);
        CHECK(snap.procs[steam].num_threads == 63);
        CHECK(snap.procs[steam].uid == getuid());
    }

    int32_t zombie = vly_snapshot_index_of(&snap, 101);
    CHECK(zombie != VLY_NO_INDEX);
    if (zombie != VLY_NO_INDEX) {
        CHECK(snap.procs[zombie].state == 'Z');
        CHECK(strcmp(snap.procs[zombie].name, "Web Content) (x") == 0);
    }

    CHECK(vly_snapshot_index_of(&snap, 999) == VLY_NO_INDEX);
    CHECK(vly_snapshot_index_of(&snap, 0) == VLY_NO_INDEX);
    CHECK(vly_snapshot_index_of(&snap, 5000) == VLY_NO_INDEX);
    CHECK(snap.timestamp_ns > 0);

    vly_snapshot_free(&snap);
}

static void test_collect_missing_root(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    CHECK(vly_snapshot_collect(&snap, VLY_FIXTURES_DIR "/does-not-exist") == -ENOENT);
    CHECK(snap.count == 0);

    vly_snapshot_free(&snap);
}

static void test_collect_live_proc(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    /* Collect twice to exercise buffer reuse. */
    for (int round = 0; round < 2; round++) {
        CHECK(vly_snapshot_collect(&snap, "/proc") == 0);

        int32_t self = vly_snapshot_index_of(&snap, getpid());
        CHECK(self != VLY_NO_INDEX);
        if (self != VLY_NO_INDEX) {
            const vly_process *p = &snap.procs[self];
            CHECK(p->ppid == getppid());
            CHECK(p->state == 'R');
            CHECK(p->uid == geteuid());
            CHECK(strcmp(p->name, "test_snapshot") == 0);
            CHECK(!p->is_kernel_thread);
            CHECK(p->rss_pages > 0);
        }
    }

    vly_snapshot_free(&snap);
}

int main(void)
{
    test_collect_fixtures();
    test_collect_missing_root();
    test_collect_live_proc();
    return TEST_RESULT();
}
