#ifndef VLY_KILL_JOBS_H
#define VLY_KILL_JOBS_H

/*
 * Stops processes in the background, so the interface keeps refreshing
 * during the SIGTERM grace period. Each job runs vly_kill_many() on its own
 * thread with a private copy of the snapshot, never touching the buffers
 * the interface keeps refreshing. Only the thread that owns the kill_jobs
 * may call these functions.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tree_view.h"
#include "vly/kill.h"
#include "vly/snapshot.h"

#define KILL_JOBS_MAX 8

typedef struct kill_job kill_job;

typedef struct kill_jobs {
    kill_job *slots[KILL_JOBS_MAX];
} kill_jobs;

/* What a finished job did. */
typedef struct kill_result {
    char name[VLY_NAME_SIZE];
    pid_t pid;
    bool tree;
    size_t descendants; /* targeted below the process, in tree mode */
    int error;          /* from vly_kill_many(), 0 if it ran */
    vly_kill_report report;
} kill_result;

void kill_jobs_init(kill_jobs *jobs);

/*
 * Starts stopping the process at `index` of `snap` (and its descendants
 * with `tree`). Returns 0, -EINVAL for processes vly never stops (pid 1,
 * kernel threads, vly itself), -EBUSY when KILL_JOBS_MAX jobs are running,
 * -ENOMEM, or an error from pthread_create().
 */
int kill_jobs_start(kill_jobs *jobs, const vly_snapshot *snap, int32_t index, bool tree,
                    const vly_kill_options *opts);

/* True while a running job is stopping `proc`. */
bool kill_jobs_is_target(const kill_jobs *jobs, const vly_process *proc);

/* True if any job is still running. */
bool kill_jobs_busy(const kill_jobs *jobs);

/* Takes the result of one finished job, if any, and frees the job. */
bool kill_jobs_reap(kill_jobs *jobs, kill_result *result);

/*
 * Writes a one-line summary of `result` for the status bar into `buf`, e.g.
 * "Stopped steam and 13 descendants (2 needed SIGKILL)". Returns true when
 * the outcome needs the user's attention (nothing or not everything was
 * stopped).
 */
bool kill_result_describe(const kill_result *result, char *buf, size_t size);

/* Waits for every running job to finish, discarding results. */
void kill_jobs_finish_all(kill_jobs *jobs);

#endif /* VLY_KILL_JOBS_H */
