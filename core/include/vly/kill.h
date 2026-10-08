#ifndef VLY_KILL_H
#define VLY_KILL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vly/process.h"
#include "vly/snapshot.h"

/*
 * All signals are delivered through pidfds after checking that the pid still
 * belongs to the process seen in the snapshot (same start time), so a pid
 * reused in the meantime is never signalled.
 */

typedef struct vly_kill_options {
    unsigned int grace_ms;     /* wait after SIGTERM before escalating to SIGKILL */
    unsigned int kill_wait_ms; /* wait after SIGKILL before giving up */
} vly_kill_options;

#define VLY_KILL_OPTIONS_DEFAULT ((vly_kill_options){.grace_ms = 5000, .kill_wait_ms = 1000})

typedef struct vly_kill_report {
    size_t targeted;   /* live processes we tried to stop */
    size_t terminated; /* exited after SIGTERM */
    size_t killed;     /* exited only after SIGKILL */
    size_t survivors;  /* still running, e.g. stuck in uninterruptible sleep (D) */
    size_t denied;     /* not ours to signal (EPERM): needs root */
    size_t vanished;   /* exited, or pid reused, before we got to it */
    size_t zombies;    /* already dead; only their parent can reap them */
} vly_kill_report;

/*
 * Sends `sig` to `proc` (0 only checks permission). Returns 0, -ESRCH if the
 * process is gone or its pid was reused, -EPERM if not permitted, or -EINVAL
 * for pid 1, kernel threads and vly itself.
 */
int vly_signal_process(const vly_process *proc, int sig);

/*
 * Stops the process at `index`: SIGTERM, wait up to grace_ms, then SIGKILL.
 * vly_kill_tree() does the same for the process and all its descendants,
 * parents first so supervisors cannot respawn children. `snap` must have its
 * tree built. vly itself is never targeted. Returns 0 (see `report` for the
 * per-process outcome), -EINVAL for pid 1, kernel threads, vly itself or a
 * bad index, or -ENOMEM.
 */
int vly_kill(const vly_snapshot *snap, int32_t index, const vly_kill_options *opts,
             vly_kill_report *report);
int vly_kill_tree(const vly_snapshot *snap, int32_t index, const vly_kill_options *opts,
                  vly_kill_report *report);

/*
 * Like vly_kill() / vly_kill_tree() for several processes at once, sharing a
 * single grace period. Overlapping trees are handled: each process is
 * signalled once. Returns -EINVAL if any index is invalid or protected.
 */
int vly_kill_many(const vly_snapshot *snap, const int32_t *indices, size_t index_count,
                  bool descendants, const vly_kill_options *opts, vly_kill_report *report);

#endif /* VLY_KILL_H */
