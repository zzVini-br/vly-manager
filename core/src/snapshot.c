#include "vly/snapshot.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "proc_reader.h"

#define INITIAL_CAPACITY 512

void vly_snapshot_init(vly_snapshot *snap)
{
    snap->procs = NULL;
    snap->count = 0;
    snap->capacity = 0;
    snap->timestamp_ns = 0;
    snap->first_root = VLY_NO_INDEX;
}

void vly_snapshot_free(vly_snapshot *snap)
{
    free(snap->procs);
    vly_snapshot_init(snap);
}

static int grow(vly_snapshot *snap)
{
    size_t new_capacity = snap->capacity == 0 ? INITIAL_CAPACITY : snap->capacity * 2;

    /* Indices are stored as int32_t elsewhere. */
    if (new_capacity > INT32_MAX) {
        return -EOVERFLOW;
    }

    vly_process *procs = realloc(snap->procs, new_capacity * sizeof(*procs));
    if (procs == NULL) {
        return -ENOMEM;
    }

    snap->procs = procs;
    snap->capacity = new_capacity;
    return 0;
}

/* Accepts only all-digit directory names, i.e. process directories. */
static bool parse_pid_name(const char *name, pid_t *out)
{
    long value = 0;

    if (*name == '\0') {
        return false;
    }

    for (const char *p = name; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + (*p - '0');
        if (value > INT_MAX) {
            return false;
        }
    }

    *out = (pid_t)value;
    return true;
}

static int compare_by_pid(const void *a, const void *b)
{
    pid_t pa = ((const vly_process *)a)->pid;
    pid_t pb = ((const vly_process *)b)->pid;
    return (pa > pb) - (pa < pb);
}

static bool is_sorted_by_pid(const vly_snapshot *snap)
{
    for (size_t i = 1; i < snap->count; i++) {
        if (snap->procs[i - 1].pid > snap->procs[i].pid) {
            return false;
        }
    }
    return true;
}

static uint64_t monotonic_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

int vly_snapshot_collect(vly_snapshot *snap, const char *proc_root)
{
    DIR *dir = opendir(proc_root);
    if (dir == NULL) {
        return -errno;
    }

    int proc_fd = dirfd(dir);
    int err = 0;
    snap->count = 0;
    snap->first_root = VLY_NO_INDEX;

    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            err = -errno;
            break;
        }

        pid_t pid;
        if (!parse_pid_name(entry->d_name, &pid)) {
            continue;
        }

        if (snap->count == snap->capacity) {
            err = grow(snap);
            if (err != 0) {
                break;
            }
        }

        /* A process that exits mid-read is simply left out. */
        vly_process *proc = &snap->procs[snap->count];
        if (vly_read_process_at(proc_fd, pid, proc) == 0) {
            proc->parent = VLY_NO_INDEX;
            proc->first_child = VLY_NO_INDEX;
            proc->next_sibling = VLY_NO_INDEX;
            snap->count++;
        }
    }

    closedir(dir);
    if (err != 0) {
        snap->count = 0;
        return err;
    }

    /* /proc usually lists pids in order already, so this is rarely needed. */
    if (!is_sorted_by_pid(snap)) {
        qsort(snap->procs, snap->count, sizeof(*snap->procs), compare_by_pid);
    }

    snap->timestamp_ns = monotonic_now_ns();
    return 0;
}

int32_t vly_snapshot_index_of(const vly_snapshot *snap, pid_t pid)
{
    size_t lo = 0;
    size_t hi = snap->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        pid_t mid_pid = snap->procs[mid].pid;

        if (mid_pid == pid) {
            return (int32_t)mid;
        }
        if (mid_pid < pid) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return VLY_NO_INDEX;
}
