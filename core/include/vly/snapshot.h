#ifndef VLY_SNAPSHOT_H
#define VLY_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "vly/process.h"

/* Marks a missing index (e.g. a pid not present in a snapshot). */
#define VLY_NO_INDEX (-1)

/*
 * Every process visible in /proc at one point in time, sorted by pid.
 * The buffer is kept between collections so steady-state refreshes do
 * not allocate.
 */
typedef struct vly_snapshot {
    vly_process *procs;
    size_t count;
    size_t capacity;
    uint64_t timestamp_ns; /* CLOCK_MONOTONIC at collection time */
    int32_t first_root;    /* first top-level process, see build_tree */
} vly_snapshot;

void vly_snapshot_init(vly_snapshot *snap);
void vly_snapshot_free(vly_snapshot *snap);

/*
 * Replaces the contents of `snap` with the processes found under
 * `proc_root` (normally "/proc"). Processes that exit while being read are
 * skipped. Returns 0 on success or a negative errno value.
 */
int vly_snapshot_collect(vly_snapshot *snap, const char *proc_root);

/* Returns the index of `pid` in `snap`, or VLY_NO_INDEX. O(log n). */
int32_t vly_snapshot_index_of(const vly_snapshot *snap, pid_t pid);

/*
 * Links every process to its parent, children and siblings. Processes whose
 * parent is not in the snapshot (pid 1, kthreadd) become roots, chained from
 * `first_root` through `next_sibling`. Siblings are ordered by pid. O(n log n).
 *
 * /proc is not read atomically, so pid reuse during a collection can, in
 * theory, produce a parent cycle; such processes are unreachable from the
 * roots for that snapshot, which keeps every traversal finite.
 */
void vly_snapshot_build_tree(vly_snapshot *snap);

/*
 * Sets cpu_percent for every process in `cur` from the CPU time it used
 * since `prev` (which may be NULL). Processes that are new, or whose pid was
 * reused by a different process, get 0. Both snapshots must be pid-sorted,
 * which vly_snapshot_collect() guarantees. O(n).
 *
 * `ticks_per_second` is sysconf(_SC_CLK_TCK); `cpu_count` is the number of
 * online CPUs, so 100% means the whole machine is busy.
 */
void vly_snapshot_compute_cpu(vly_snapshot *cur, const vly_snapshot *prev, long ticks_per_second,
                              long cpu_count);

#endif /* VLY_SNAPSHOT_H */
