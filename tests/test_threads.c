#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "test.h"
#include "vly/snapshot.h"

static void test_fixture_threads(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    CHECK(vly_snapshot_collect_threads(&snap, VLY_FIXTURES_DIR "/proc", 100) == 0);
    CHECK(snap.count == 3);
    if (snap.count == 3) {
        CHECK(snap.procs[0].pid == 100);
        CHECK(strcmp(snap.procs[0].name, "steam") == 0);
        CHECK(snap.procs[1].pid == 105);
        CHECK(strcmp(snap.procs[1].name, "steam:disk$0") == 0);
        CHECK(snap.procs[1].state == 'D');
        CHECK(snap.procs[2].pid == 107);
        CHECK(snap.procs[2].utime == 300);
    }

    vly_snapshot_free(&snap);
}

static void test_missing_process(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    CHECK(vly_snapshot_collect_threads(&snap, VLY_FIXTURES_DIR "/proc", 4242) == -ENOENT);
    CHECK(snap.count == 0);

    vly_snapshot_free(&snap);
}

/* Threads park on a barrier-like pipe until the test is done with them. */
typedef struct worker {
    const char *name;
    int ready_fd;
    int release_fd;
} worker;

static void *worker_main(void *arg)
{
    const worker *w = arg;
    char c;

    prctl(PR_SET_NAME, w->name, 0, 0, 0);
    (void)!write(w->ready_fd, "x", 1);
    (void)!read(w->release_fd, &c, 1);
    return NULL;
}

static void test_live_threads(void)
{
    int ready[2];
    int release[2];
    if (pipe(ready) != 0 || pipe(release) != 0) {
        CHECK(false);
        return;
    }

    worker workers[] = {
        {.name = "vly-worker-a", .ready_fd = ready[1], .release_fd = release[0]},
        {.name = "vly-worker-b", .ready_fd = ready[1], .release_fd = release[0]},
    };
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) {
        pthread_create(&threads[i], NULL, worker_main, &workers[i]);
    }
    char c;
    for (int i = 0; i < 2; i++) {
        (void)!read(ready[0], &c, 1);
    }

    vly_snapshot snap;
    vly_snapshot_init(&snap);
    CHECK(vly_snapshot_collect_threads(&snap, "/proc", getpid()) == 0);

    /* The main thread plus the two workers. */
    CHECK(snap.count == 3);
    CHECK(vly_snapshot_index_of(&snap, getpid()) != VLY_NO_INDEX);

    bool found_a = false;
    bool found_b = false;
    for (size_t i = 0; i < snap.count; i++) {
        found_a |= strcmp(snap.procs[i].name, "vly-worker-a") == 0;
        found_b |= strcmp(snap.procs[i].name, "vly-worker-b") == 0;
    }
    CHECK(found_a);
    CHECK(found_b);

    (void)!write(release[1], "xx", 2);
    for (int i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
    }
    vly_snapshot_free(&snap);
}

int main(void)
{
    test_fixture_threads();
    test_missing_process();
    test_live_threads();
    return TEST_RESULT();
}
