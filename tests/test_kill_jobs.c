#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "kill_jobs.h"
#include "test.h"

static const vly_kill_options fast = {.grace_ms = 200, .kill_wait_ms = 1000};

/* Forks a child that sleeps until signalled, optionally ignoring SIGTERM,
 * and optionally with one grandchild of its own that ignores SIGTERM. */
static pid_t spawn(bool ignore_term, bool with_child)
{
    int ready[2];
    if (pipe(ready) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
        if (with_child) {
            spawn(true, false);
        }
        if (ignore_term) {
            signal(SIGTERM, SIG_IGN);
        }
        (void)!write(ready[1], "x", 1);
        close(ready[1]);
        for (;;) {
            pause();
        }
    }

    close(ready[1]);
    char c;
    (void)!read(ready[0], &c, 1);
    close(ready[0]);
    return pid;
}

static int32_t take_snapshot(vly_snapshot *snap, pid_t pid)
{
    vly_snapshot_collect(snap, "/proc");
    vly_snapshot_build_tree(snap);
    return vly_snapshot_index_of(snap, pid);
}

/* Polls for a finished job for up to 5 s, as the interface's loop does. */
static bool wait_for_result(kill_jobs *jobs, kill_result *result)
{
    struct timespec pause_10ms = {.tv_sec = 0, .tv_nsec = 10000000L};
    for (int tries = 0; tries < 500; tries++) {
        if (kill_jobs_reap(jobs, result)) {
            return true;
        }
        nanosleep(&pause_10ms, NULL);
    }
    return false;
}

static void test_stop_one_process(void)
{
    kill_jobs jobs;
    kill_jobs_init(&jobs);
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    pid_t child = spawn(false, false);
    int32_t index = take_snapshot(&snap, child);
    CHECK(kill_jobs_start(&jobs, &snap, index, false, &fast) == 0);

    /* The snapshot can change right away: the job has its own copy. */
    vly_snapshot_free(&snap);

    kill_result result;
    CHECK(wait_for_result(&jobs, &result));
    CHECK(result.error == 0);
    CHECK(result.pid == child);
    CHECK(!result.tree);
    CHECK(result.report.terminated == 1);
    CHECK(!kill_jobs_busy(&jobs));

    waitpid(child, NULL, 0);
}

static void test_stop_tree_and_mark_targets(void)
{
    kill_jobs jobs;
    kill_jobs_init(&jobs);
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    pid_t parent = spawn(false, true);
    int32_t index = take_snapshot(&snap, parent);
    int32_t grandchild = snap.procs[index].first_child;
    CHECK(grandchild != VLY_NO_INDEX);

    /* A longer grace period keeps the job running while we look at it. */
    vly_kill_options slow = {.grace_ms = 500, .kill_wait_ms = 1000};
    CHECK(kill_jobs_start(&jobs, &snap, index, true, &slow) == 0);
    CHECK(kill_jobs_busy(&jobs));
    CHECK(kill_jobs_is_target(&jobs, &snap.procs[index]));
    if (grandchild != VLY_NO_INDEX) {
        CHECK(kill_jobs_is_target(&jobs, &snap.procs[grandchild]));
    }
    int32_t self = vly_snapshot_index_of(&snap, getpid());
    CHECK(!kill_jobs_is_target(&jobs, &snap.procs[self]));

    kill_result result;
    CHECK(wait_for_result(&jobs, &result));
    CHECK(result.tree);
    CHECK(result.descendants == 1);
    CHECK(result.report.terminated == 1); /* the parent */
    CHECK(result.report.killed == 1);     /* the grandchild ignoring SIGTERM */

    waitpid(parent, NULL, 0);
    vly_snapshot_free(&snap);
}

static void test_refuses_protected_processes(void)
{
    kill_jobs jobs;
    kill_jobs_init(&jobs);
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    int32_t init = take_snapshot(&snap, 1);
    CHECK(kill_jobs_start(&jobs, &snap, init, true, &fast) == -EINVAL);
    CHECK(kill_jobs_start(&jobs, &snap, vly_snapshot_index_of(&snap, getpid()), false, &fast) ==
          -EINVAL);
    CHECK(kill_jobs_start(&jobs, &snap, VLY_NO_INDEX, false, &fast) == -EINVAL);
    CHECK(!kill_jobs_busy(&jobs));

    vly_snapshot_free(&snap);
}

static void test_limits_concurrent_jobs(void)
{
    kill_jobs jobs;
    kill_jobs_init(&jobs);
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    pid_t children[KILL_JOBS_MAX + 1];
    for (int i = 0; i <= KILL_JOBS_MAX; i++) {
        children[i] = spawn(true, false);
    }
    take_snapshot(&snap, getpid());

    vly_kill_options slow = {.grace_ms = 300, .kill_wait_ms = 1000};
    for (int i = 0; i < KILL_JOBS_MAX; i++) {
        CHECK(kill_jobs_start(&jobs, &snap, vly_snapshot_index_of(&snap, children[i]), false,
                              &slow) == 0);
    }
    CHECK(kill_jobs_start(&jobs, &snap, vly_snapshot_index_of(&snap, children[KILL_JOBS_MAX]),
                          false, &slow) == -EBUSY);

    kill_jobs_finish_all(&jobs);
    CHECK(!kill_jobs_busy(&jobs));
    kill_result result;
    CHECK(!kill_jobs_reap(&jobs, &result));

    for (int i = 0; i <= KILL_JOBS_MAX; i++) {
        kill(children[i], SIGKILL);
        waitpid(children[i], NULL, 0);
    }
    vly_snapshot_free(&snap);
}

static bool describes(const kill_result *result, const char *expected, bool attention)
{
    char buf[160];
    bool needs_attention = kill_result_describe(result, buf, sizeof(buf));
    if (strcmp(buf, expected) != 0 || needs_attention != attention) {
        fprintf(stderr, "  got \"%s\" (%d), expected \"%s\" (%d)\n", buf, needs_attention,
                expected, attention);
        return false;
    }
    return true;
}

static void test_describe_results(void)
{
    kill_result r;
    memset(&r, 0, sizeof(r));
    snprintf(r.name, sizeof(r.name), "steam");
    r.pid = 4242;

    r.report.terminated = 1;
    CHECK(describes(&r, "Stopped steam", false));
    r.report.terminated = 0;
    r.report.killed = 1;
    CHECK(describes(&r, "Stopped steam (needed SIGKILL)", false));

    r.tree = true;
    r.report.terminated = 12;
    r.report.killed = 2;
    CHECK(describes(&r, "Stopped steam and 13 descendants (2 needed SIGKILL)", false));
    r.report.terminated = 2;
    r.report.killed = 0;
    CHECK(describes(&r, "Stopped steam and 1 descendant", false));

    memset(&r.report, 0, sizeof(r.report));
    r.report.denied = 3;
    CHECK(describes(&r, "steam: 3 processes belong to another user; run vly with sudo", true));

    memset(&r.report, 0, sizeof(r.report));
    r.report.survivors = 1;
    CHECK(describes(&r, "steam: 1 process still running even after SIGKILL (stuck in D state?)",
                    true));

    memset(&r.report, 0, sizeof(r.report));
    r.report.failed = 2;
    CHECK(describes(&r, "steam: 2 processes were not stopped because of a system error", true));

    memset(&r.report, 0, sizeof(r.report));
    r.report.zombies = 1;
    CHECK(describes(&r, "steam is a zombie: already dead, only its parent can clear it", true));

    memset(&r.report, 0, sizeof(r.report));
    r.report.vanished = 1;
    CHECK(describes(&r, "steam had already exited", false));

    r.error = -EINVAL;
    CHECK(describes(&r, "Could not stop steam: vly never stops pid 1, kernel threads or itself",
                    true));

    /* Names are made printable. */
    r.error = 0;
    memset(&r.report, 0, sizeof(r.report));
    r.report.terminated = 1;
    r.tree = false;
    snprintf(r.name, sizeof(r.name), "e\033[2J");
    CHECK(describes(&r, "Stopped e?[2J", false));
}

int main(void)
{
    test_describe_results();
    test_stop_one_process();
    test_stop_tree_and_mark_targets();
    test_refuses_protected_processes();
    test_limits_concurrent_jobs();
    return TEST_RESULT();
}
