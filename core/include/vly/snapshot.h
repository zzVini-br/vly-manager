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

#endif /* VLY_SNAPSHOT_H */
