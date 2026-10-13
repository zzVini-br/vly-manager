#include "kill_jobs.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct kill_job {
    pthread_t thread;
    vly_snapshot snap; /* private copy, owned by the job */
    int32_t index;
    bool tree;
    vly_kill_options opts;
    proc_key *targets; /* the process and, in tree mode, its descendants */
    size_t target_count;
    atomic_bool done;
    kill_result result;
};

void kill_jobs_init(kill_jobs *jobs)
{
    memset(jobs, 0, sizeof(*jobs));
}

static bool is_protected(const vly_process *proc)
{
    return proc->pid <= 1 || proc->is_kernel_thread || proc->pid == getpid();
}

static int copy_snapshot(vly_snapshot *dst, const vly_snapshot *src)
{
    vly_snapshot_init(dst);
    dst->procs = malloc(src->count * sizeof(*dst->procs));
    if (dst->procs == NULL) {
        return -ENOMEM;
    }

    memcpy(dst->procs, src->procs, src->count * sizeof(*dst->procs));
    dst->count = src->count;
    dst->capacity = src->count;
    dst->timestamp_ns = src->timestamp_ns;
    dst->first_root = src->first_root;
    return 0;
}

/* Lists the process at `index` and, in tree mode, its descendants. The
 * list is capped at the snapshot size, so a pid-reuse cycle cannot make it
 * grow without bound. */
static int collect_targets(kill_job *job)
{
    const vly_snapshot *snap = &job->snap;
    int32_t *stack = malloc(snap->count * sizeof(*stack));
    job->targets = malloc(snap->count * sizeof(*job->targets));
    if (stack == NULL || job->targets == NULL) {
        free(stack);
        return -ENOMEM;
    }

    size_t depth = 0;
    stack[depth++] = job->index;
    while (depth > 0 && job->target_count < snap->count) {
        const vly_process *proc = &snap->procs[stack[--depth]];
        job->targets[job->target_count++] =
            (proc_key){.pid = proc->pid, .start_time = proc->start_time};

        if (!job->tree) {
            break;
        }
        for (int32_t c = proc->first_child; c != VLY_NO_INDEX && depth < snap->count;
             c = snap->procs[c].next_sibling) {
            stack[depth++] = c;
        }
    }

    free(stack);
    return 0;
}

static void free_job(kill_job *job)
{
    vly_snapshot_free(&job->snap);
    free(job->targets);
    free(job);
}

static void *run_job(void *arg)
{
    kill_job *job = arg;

    job->result.error = vly_kill_many(&job->snap, &job->index, 1, job->tree, &job->opts,
                                      &job->result.report);
    atomic_store(&job->done, true);
    return NULL;
}

int kill_jobs_start(kill_jobs *jobs, const vly_snapshot *snap, int32_t index, bool tree,
                    const vly_kill_options *opts)
{
    if (index < 0 || (size_t)index >= snap->count || is_protected(&snap->procs[index])) {
        return -EINVAL;
    }

    size_t slot = 0;
    while (slot < KILL_JOBS_MAX && jobs->slots[slot] != NULL) {
        slot++;
    }
    if (slot == KILL_JOBS_MAX) {
        return -EBUSY;
    }

    kill_job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return -ENOMEM;
    }
    job->index = index;
    job->tree = tree;
    job->opts = opts != NULL ? *opts : VLY_KILL_OPTIONS_DEFAULT;
    atomic_init(&job->done, false);

    int err = copy_snapshot(&job->snap, snap);
    if (err == 0) {
        err = collect_targets(job);
    }
    if (err != 0) {
        free_job(job);
        return err;
    }

    const vly_process *proc = &snap->procs[index];
    memcpy(job->result.name, proc->name, sizeof(job->result.name));
    job->result.pid = proc->pid;
    job->result.tree = tree;
    job->result.descendants = job->target_count - 1;

    err = pthread_create(&job->thread, NULL, run_job, job);
    if (err != 0) {
        free_job(job);
        return -err;
    }

    jobs->slots[slot] = job;
    return 0;
}

bool kill_jobs_is_target(const kill_jobs *jobs, const vly_process *proc)
{
    for (size_t slot = 0; slot < KILL_JOBS_MAX; slot++) {
        const kill_job *job = jobs->slots[slot];
        if (job == NULL) {
            continue;
        }
        for (size_t i = 0; i < job->target_count; i++) {
            if (job->targets[i].pid == proc->pid &&
                job->targets[i].start_time == proc->start_time) {
                return true;
            }
        }
    }
    return false;
}

bool kill_jobs_busy(const kill_jobs *jobs)
{
    for (size_t slot = 0; slot < KILL_JOBS_MAX; slot++) {
        if (jobs->slots[slot] != NULL && !atomic_load(&jobs->slots[slot]->done)) {
            return true;
        }
    }
    return false;
}

bool kill_jobs_reap(kill_jobs *jobs, kill_result *result)
{
    for (size_t slot = 0; slot < KILL_JOBS_MAX; slot++) {
        kill_job *job = jobs->slots[slot];
        if (job != NULL && atomic_load(&job->done)) {
            pthread_join(job->thread, NULL);
            *result = job->result;
            free_job(job);
            jobs->slots[slot] = NULL;
            return true;
        }
    }
    return false;
}

static const char *plural(size_t n, const char *one, const char *many)
{
    return n == 1 ? one : many;
}

bool kill_result_describe(const kill_result *result, char *buf, size_t size)
{
    const vly_kill_report *r = &result->report;
    size_t stopped = r->terminated + r->killed;

    /* fmt_safe_name() lives with the curses code; the name only needs to be
     * printable here, so control characters become '?'. */
    char name[VLY_NAME_SIZE];
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)result->name; *p != '\0'; p++) {
        name[len++] = *p < 0x20 || *p == 0x7f ? '?' : (char)*p;
    }
    name[len] = '\0';

    if (result->error != 0) {
        snprintf(buf, size, "Could not stop %s: %s", name,
                 result->error == -EINVAL ? "vly never stops pid 1, kernel threads or itself"
                                          : strerror(-result->error));
        return true;
    }
    if (r->denied > 0) {
        snprintf(buf, size, "%s: %zu %s another user; run vly with sudo", name, r->denied,
                 plural(r->denied, "process belongs to", "processes belong to"));
        return true;
    }
    if (r->survivors > 0) {
        snprintf(buf, size, "%s: %zu %s running even after SIGKILL (stuck in D state?)", name,
                 r->survivors, plural(r->survivors, "process still", "processes still"));
        return true;
    }
    if (r->failed > 0) {
        snprintf(buf, size, "%s: %zu %s not stopped because of a system error", name, r->failed,
                 plural(r->failed, "process was", "processes were"));
        return true;
    }
    if (stopped == 0 && r->zombies > 0) {
        snprintf(buf, size, "%s is a zombie: already dead, only its parent can clear it", name);
        return true;
    }
    if (stopped == 0) {
        snprintf(buf, size, "%s had already exited", name);
        return false;
    }

    char detail[64] = "";
    if (r->killed > 0) {
        snprintf(detail, sizeof(detail), " (%zu needed SIGKILL)", r->killed);
    }
    if (result->tree && stopped > 1) {
        snprintf(buf, size, "Stopped %s and %zu %s%s", name, stopped - 1,
                 plural(stopped - 1, "descendant", "descendants"), detail);
    } else {
        snprintf(buf, size, "Stopped %s%s", name, r->killed > 0 ? " (needed SIGKILL)" : "");
    }
    return false;
}

void kill_jobs_finish_all(kill_jobs *jobs)
{
    for (size_t slot = 0; slot < KILL_JOBS_MAX; slot++) {
        if (jobs->slots[slot] != NULL) {
            pthread_join(jobs->slots[slot]->thread, NULL);
            free_job(jobs->slots[slot]);
            jobs->slots[slot] = NULL;
        }
    }
}
