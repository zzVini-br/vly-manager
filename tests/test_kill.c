#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "test.h"
#include "vly/kill.h"

static const vly_kill_options fast = {.grace_ms = 200, .kill_wait_ms = 1000};

/* Forks a child that sleeps until signalled, optionally ignoring SIGTERM.
 * Returns once the child has set up its signal handling. */
static pid_t spawn(bool ignore_term)
{
    int ready[2];
    if (pipe(ready) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
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

/* Forks a parent with two children: one that ignores SIGTERM, one that
 * doesn't. Returns once the whole tree is up. */
static pid_t spawn_tree(void)
{
    int ready[2];
    if (pipe(ready) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
        spawn(true);
        spawn(false);
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

static void sleep_ms(long ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void test_kill_cooperative_process(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t child = spawn(false);
    int32_t index = take_snapshot(&snap, child);
    CHECK(index != VLY_NO_INDEX);

    vly_kill_report report;
    CHECK(vly_kill(&snap, index, &fast, &report) == 0);
    CHECK(report.targeted == 1);
    CHECK(report.terminated == 1);
    CHECK(report.killed == 0);
    CHECK(report.survivors == 0);

    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    vly_snapshot_free(&snap);
}

static void test_kill_escalates_to_sigkill(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t child = spawn(true);
    int32_t index = take_snapshot(&snap, child);

    vly_kill_report report;
    CHECK(vly_kill(&snap, index, &fast, &report) == 0);
    CHECK(report.targeted == 1);
    CHECK(report.terminated == 0);
    CHECK(report.killed == 1);

    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    vly_snapshot_free(&snap);
}

static void test_kill_tree(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t parent = spawn_tree();
    int32_t index = take_snapshot(&snap, parent);

    vly_kill_report report;
    CHECK(vly_kill_tree(&snap, index, &fast, &report) == 0);
    CHECK(report.targeted == 3);
    CHECK(report.terminated == 2); /* the parent and the cooperative child */
    CHECK(report.killed == 1);     /* the child ignoring SIGTERM */
    CHECK(report.survivors == 0);

    CHECK(waitpid(parent, NULL, 0) == parent);
    vly_snapshot_free(&snap);
}

static void test_kill_many_shares_grace_and_dedupes(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t tree = spawn_tree();
    pid_t stubborn = spawn(true);
    int32_t tree_index = take_snapshot(&snap, tree);
    int32_t stubborn_index = vly_snapshot_index_of(&snap, stubborn);

    /* The tree's first child is listed too: it must be signalled once. */
    int32_t roots[] = {tree_index, stubborn_index, snap.procs[tree_index].first_child};

    vly_kill_report report;
    CHECK(vly_kill_many(&snap, roots, 3, true, &fast, &report) == 0);
    CHECK(report.targeted == 4);
    CHECK(report.terminated == 2);
    CHECK(report.killed == 2);

    CHECK(waitpid(tree, NULL, 0) == tree);
    CHECK(waitpid(stubborn, NULL, 0) == stubborn);
    vly_snapshot_free(&snap);
}

static void test_zombie_is_not_targeted(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);

    pid_t child = fork();
    if (child == 0) {
        _exit(0);
    }

    /* Not reaped yet, so it turns into a zombie. */
    int32_t index = VLY_NO_INDEX;
    for (int tries = 0; tries < 100; tries++) {
        index = take_snapshot(&snap, child);
        if (index != VLY_NO_INDEX && snap.procs[index].state == 'Z') {
            break;
        }
        sleep_ms(10);
    }

    vly_kill_report report;
    CHECK(vly_kill(&snap, index, &fast, &report) == 0);
    CHECK(report.zombies == 1);
    CHECK(report.targeted == 0);

    waitpid(child, NULL, 0);
    vly_snapshot_free(&snap);
}

static void test_exited_process_is_vanished(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t child = spawn(false);
    int32_t index = take_snapshot(&snap, child);

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);

    vly_kill_report report;
    CHECK(vly_kill(&snap, index, &fast, &report) == 0);
    CHECK(report.vanished == 1);
    CHECK(report.targeted == 0);
    vly_snapshot_free(&snap);
}

static void test_reused_pid_is_not_signalled(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t child = spawn(false);
    int32_t index = take_snapshot(&snap, child);

    /* Pretend the snapshot saw a different process with this pid. */
    snap.procs[index].start_time++;

    vly_kill_report report;
    CHECK(vly_kill(&snap, index, &fast, &report) == 0);
    CHECK(report.vanished == 1);
    CHECK(kill(child, 0) == 0); /* still alive */
    CHECK(vly_signal_process(&snap.procs[index], SIGKILL) == -ESRCH);

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    vly_snapshot_free(&snap);
}

static void test_protected_targets(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    int32_t init = take_snapshot(&snap, 1);
    int32_t kthreadd = vly_snapshot_index_of(&snap, 2);
    int32_t self = vly_snapshot_index_of(&snap, getpid());

    vly_kill_report report;
    CHECK(vly_kill(&snap, init, &fast, &report) == -EINVAL);
    CHECK(vly_kill_tree(&snap, kthreadd, &fast, &report) == -EINVAL);
    CHECK(vly_kill(&snap, self, &fast, &report) == -EINVAL);
    CHECK(vly_kill(&snap, VLY_NO_INDEX, &fast, &report) == -EINVAL);
    CHECK(vly_kill(&snap, (int32_t)snap.count, &fast, &report) == -EINVAL);
    CHECK(vly_signal_process(&snap.procs[init], 0) == -EINVAL);
    vly_snapshot_free(&snap);
}

static void test_signal_permissions(void)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    pid_t child = spawn(false);
    int32_t index = take_snapshot(&snap, child);

    CHECK(vly_signal_process(&snap.procs[index], 0) == 0);

    /* As a regular user, another user's process is off limits. Signal 0
     * only checks permission, so nothing is actually sent. */
    if (geteuid() != 0) {
        for (size_t i = 0; i < snap.count; i++) {
            const vly_process *p = &snap.procs[i];
            if (p->uid == 0 && p->pid > 1 && !p->is_kernel_thread && p->state != 'Z') {
                CHECK(vly_signal_process(p, 0) == -EPERM);
                break;
            }
        }
    }

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    vly_snapshot_free(&snap);
}

/* vly running inside the tree it kills must take down its ancestor but
 * survive itself. */
static void test_tree_kill_spares_caller(void)
{
    int result[2];
    if (pipe(result) != 0) {
        CHECK(false);
        return;
    }

    pid_t ancestor = fork();
    if (ancestor == 0) {
        close(result[0]);
        if (fork() == 0) {
            vly_snapshot snap;
            vly_snapshot_init(&snap);
            int32_t index = take_snapshot(&snap, getppid());
            vly_kill_report report = {0};
            vly_kill_tree(&snap, index, &fast, &report);
            (void)!write(result[1], &report, sizeof(report));
            _exit(0);
        }
        for (;;) {
            pause();
        }
    }

    close(result[1]);
    vly_kill_report report = {0};
    CHECK(read(result[0], &report, sizeof(report)) == (ssize_t)sizeof(report));
    close(result[0]);

    CHECK(report.targeted == 1);
    CHECK(report.terminated == 1);
    CHECK(waitpid(ancestor, NULL, 0) == ancestor);
}

int main(void)
{
    test_kill_cooperative_process();
    test_kill_escalates_to_sigkill();
    test_kill_tree();
    test_kill_many_shares_grace_and_dedupes();
    test_zombie_is_not_targeted();
    test_exited_process_is_vanished();
    test_reused_pid_is_not_signalled();
    test_protected_targets();
    test_signal_permissions();
    test_tree_kill_spares_caller();
    return TEST_RESULT();
}
