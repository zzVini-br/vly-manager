#include "vly/snapshot.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_CAPACITY 512

/* A stat line is a few hundred bytes; everything vly needs (up to the rss
 * field) fits well within this even with the longest process name. */
#define STAT_BUFFER_SIZE 1024

void vly_snapshot_init(vly_snapshot *snap)
{
    snap->procs = NULL;
    snap->count = 0;
    snap->capacity = 0;
    snap->timestamp_ns = 0;
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

static int read_process(int proc_fd, pid_t pid, vly_process *out)
{
    char path[32];
    snprintf(path, sizeof(path), "%d/stat", (int)pid);

    int fd = openat(proc_fd, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }

    /* The owner of /proc/PID/stat is the process's effective uid; this is
     * the same source `ps` uses and costs no extra path lookup. */
    struct stat st;
    char buf[STAT_BUFFER_SIZE];
    ssize_t len = -1;
    int err = 0;

    if (fstat(fd, &st) != 0) {
        err = -errno;
    } else {
        len = read(fd, buf, sizeof(buf) - 1);
        if (len <= 0) {
            err = len < 0 ? -errno : -ENODATA;
        }
    }
    close(fd);

    if (err != 0) {
        return err;
    }

    buf[len] = '\0';
    err = vly_parse_stat(buf, out);
    if (err != 0) {
        return err;
    }

    out->uid = st.st_uid;
    return 0;
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
        if (read_process(proc_fd, pid, &snap->procs[snap->count]) == 0) {
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
