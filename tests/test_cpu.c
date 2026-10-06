#include <stdbool.h>
#include <string.h>

#include "test.h"
#include "vly/snapshot.h"

#define TICKS_PER_SECOND 100
#define CPU_COUNT 4
#define ONE_SECOND_NS 1000000000ull

static vly_process make_process(pid_t pid, unsigned long long start_time, unsigned long long utime,
                                unsigned long long stime)
{
    vly_process p;
    memset(&p, 0, sizeof(p));
    p.pid = pid;
    p.start_time = start_time;
    p.utime = utime;
    p.stime = stime;
    p.cpu_percent = -1.0; /* must be overwritten */
    return p;
}

static vly_snapshot wrap(vly_process *procs, size_t count, uint64_t timestamp_ns)
{
    vly_snapshot snap;
    vly_snapshot_init(&snap);
    snap.procs = procs;
    snap.count = count;
    snap.capacity = count;
    snap.timestamp_ns = timestamp_ns;
    return snap;
}

static bool near(double a, double b)
{
    double diff = a - b;
    return diff < 1e-9 && diff > -1e-9;
}

static void test_cpu_between_snapshots(void)
{
    vly_process before[] = {
        make_process(10, 1, 100, 100),
        make_process(20, 5, 0, 0),
        make_process(30, 7, 50, 0),
    };
    vly_process after[] = {
        make_process(5, 900, 10, 10),    /* new: not in `before` */
        make_process(10, 1, 300, 100),   /* +200 ticks */
        make_process(20, 5, 0, 0),       /* idle */
        make_process(30, 950, 80, 0),    /* pid 30 reused by another process */
        make_process(40, 960, 400, 400), /* new, past the end of `before` */
    };

    vly_snapshot prev = wrap(before, 3, 10 * ONE_SECOND_NS);
    vly_snapshot cur = wrap(after, 5, 11 * ONE_SECOND_NS);
    vly_snapshot_compute_cpu(&cur, &prev, TICKS_PER_SECOND, CPU_COUNT);

    /* 200 ticks = 2 CPU-seconds, out of 4 CPUs x 1 s = 50%. */
    CHECK(near(after[1].cpu_percent, 50.0));
    CHECK(near(after[0].cpu_percent, 0.0));
    CHECK(near(after[2].cpu_percent, 0.0));
    CHECK(near(after[3].cpu_percent, 0.0));
    CHECK(near(after[4].cpu_percent, 0.0));
}

static void test_cpu_is_clamped(void)
{
    vly_process before[] = {make_process(10, 1, 0, 0)};
    vly_process after[] = {make_process(10, 1, 1000, 1000)};

    vly_snapshot prev = wrap(before, 1, ONE_SECOND_NS);
    vly_snapshot cur = wrap(after, 1, 2 * ONE_SECOND_NS);
    vly_snapshot_compute_cpu(&cur, &prev, TICKS_PER_SECOND, CPU_COUNT);

    CHECK(near(after[0].cpu_percent, 100.0));
}

static void test_cpu_without_usable_previous(void)
{
    vly_process before[] = {make_process(10, 1, 0, 0)};
    vly_process after[] = {make_process(10, 1, 100, 0)};
    vly_snapshot prev = wrap(before, 1, 5 * ONE_SECOND_NS);
    vly_snapshot cur = wrap(after, 1, 5 * ONE_SECOND_NS);

    vly_snapshot_compute_cpu(&cur, NULL, TICKS_PER_SECOND, CPU_COUNT);
    CHECK(near(after[0].cpu_percent, 0.0));

    after[0].cpu_percent = -1.0;
    vly_snapshot_compute_cpu(&cur, &prev, TICKS_PER_SECOND, CPU_COUNT); /* zero elapsed time */
    CHECK(near(after[0].cpu_percent, 0.0));
}

int main(void)
{
    test_cpu_between_snapshots();
    test_cpu_is_clamped();
    test_cpu_without_usable_previous();
    return TEST_RESULT();
}
