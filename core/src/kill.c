#include "vly/kill.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/pidfd.h>
#include <time.h>
#include <unistd.h>

#include "proc_reader.h"

typedef struct target {
    int pidfd; /* -1 once the process has exited or been dropped */
    char state;
} target;

static bool is_protected(const vly_process *proc)
{
    return proc->pid <= 1 || proc->is_kernel_thread || proc->pid == getpid();
}

static int open_proc_dir(void)
{
    int fd = open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

/*
 * Opens a pidfd for `proc` and confirms it still refers to the process from
 * the snapshot. The pidfd pins whatever process owns the pid right now; if
 * /proc then shows the same start time, that process is the one we saw.
 */
static int open_verified(int proc_fd, const vly_process *proc, int *pidfd_out, char *state_out)
{
    int pidfd = pidfd_open(proc->pid, 0);
    if (pidfd < 0) {
        return -errno;
    }

    vly_process now;
    int err = vly_read_process_at(proc_fd, proc->pid, &now);
    if (err == -ENOENT || (err == 0 && now.start_time != proc->start_time)) {
        err = -ESRCH;
    }
    if (err != 0) {
        close(pidfd);
        return err;
    }

    *pidfd_out = pidfd;
    *state_out = now.state;
    return 0;
}

int vly_signal_process(const vly_process *proc, int sig)
{
    if (is_protected(proc)) {
        return -EINVAL;
    }

    int proc_fd = open_proc_dir();
    if (proc_fd < 0) {
        return proc_fd;
    }

    int pidfd;
    char state;
    int err = open_verified(proc_fd, proc, &pidfd, &state);
    close(proc_fd);
    if (err != 0) {
        return err;
    }

    if (pidfd_send_signal(pidfd, sig, NULL, 0) != 0) {
        err = -errno;
    }
    close(pidfd);
    return err;
}

/* Lists the roots and, if requested, their descendants, each parent before
 * its children and each process once even when roots overlap. `seen`,
 * `stack` and `out` must each hold snap->count entries; `seen` must start
 * all false. */
static size_t collect_targets(const vly_snapshot *snap, const int32_t *roots, size_t root_count,
                              bool descendants, bool *seen, int32_t *stack, int32_t *out)
{
    size_t count = 0;

    for (size_t r = 0; r < root_count; r++) {
        if (seen[roots[r]]) {
            continue;
        }

        /* Marking on push bounds the stack by snap->count. */
        size_t depth = 0;
        stack[depth++] = roots[r];
        seen[roots[r]] = true;

        while (depth > 0) {
            int32_t i = stack[--depth];
            out[count++] = i;

            if (!descendants) {
                continue;
            }
            for (int32_t c = snap->procs[i].first_child; c != VLY_NO_INDEX;
                 c = snap->procs[c].next_sibling) {
                if (!seen[c]) {
                    seen[c] = true;
                    stack[depth++] = c;
                }
            }
        }
    }

    return count;
}

static void drop(target *t)
{
    close(t->pidfd);
    t->pidfd = -1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* Waits up to `timeout_ms` for targets to exit (a pidfd becomes readable when
 * its process exits). Returns how many exited; they are dropped. */
static size_t wait_for_exit(target *targets, struct pollfd *fds, size_t n, unsigned int timeout_ms)
{
    size_t pending = 0;
    for (size_t i = 0; i < n; i++) {
        fds[i].fd = targets[i].pidfd; /* poll() ignores negative fds */
        fds[i].events = POLLIN;
        fds[i].revents = 0;
        pending += targets[i].pidfd >= 0;
    }

    size_t exited = 0;
    uint64_t deadline = monotonic_ms() + timeout_ms;

    while (pending > 0) {
        uint64_t now = monotonic_ms();
        int remaining = now >= deadline ? 0 : (int)(deadline - now);

        int ready = poll(fds, (nfds_t)n, remaining);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }

        for (size_t i = 0; i < n; i++) {
            if (fds[i].fd >= 0 && fds[i].revents != 0) {
                drop(&targets[i]);
                fds[i].fd = -1;
                exited++;
                pending--;
            }
        }
    }

    return exited;
}

/* Sends `sig` to every live target. Targets that refuse (EPERM) are counted
 * as denied and dropped; ones that already exited are left for
 * wait_for_exit() to notice. */
static void signal_all(target *targets, size_t n, int sig, vly_kill_report *report)
{
    for (size_t i = 0; i < n; i++) {
        if (targets[i].pidfd < 0) {
            continue;
        }
        if (pidfd_send_signal(targets[i].pidfd, sig, NULL, 0) != 0 && errno == EPERM) {
            drop(&targets[i]);
            report->denied++;
            report->targeted--;
        }
    }
}

int vly_kill_many(const vly_snapshot *snap, const int32_t *indices, size_t index_count,
                  bool descendants, const vly_kill_options *opts, vly_kill_report *report)
{
    memset(report, 0, sizeof(*report));

    for (size_t r = 0; r < index_count; r++) {
        int32_t index = indices[r];
        if (index < 0 || (size_t)index >= snap->count || is_protected(&snap->procs[index])) {
            return -EINVAL;
        }
    }
    if (index_count == 0) {
        return 0;
    }

    vly_kill_options defaults = VLY_KILL_OPTIONS_DEFAULT;
    if (opts == NULL) {
        opts = &defaults;
    }

    int32_t *order = malloc(2 * snap->count * sizeof(*order));
    bool *seen = calloc(snap->count, sizeof(*seen));
    target *targets = malloc(snap->count * sizeof(*targets));
    struct pollfd *fds = malloc(snap->count * sizeof(*fds));
    int proc_fd = open_proc_dir();
    int err = 0;

    if (order == NULL || seen == NULL || targets == NULL || fds == NULL) {
        err = -ENOMEM;
        goto out;
    }
    if (proc_fd < 0) {
        err = proc_fd;
        goto out;
    }

    size_t count = collect_targets(snap, indices, index_count, descendants, seen,
                                   order + snap->count, order);
    size_t n = 0;

    for (size_t i = 0; i < count; i++) {
        const vly_process *proc = &snap->procs[order[i]];
        if (is_protected(proc)) {
            continue;
        }

        target t;
        if (open_verified(proc_fd, proc, &t.pidfd, &t.state) != 0) {
            report->vanished++;
        } else if (t.state == 'Z') {
            report->zombies++;
            close(t.pidfd);
        } else {
            targets[n++] = t;
        }
    }
    report->targeted = n;

    signal_all(targets, n, SIGTERM, report);
    /* A stopped process keeps SIGTERM pending until it is continued. */
    for (size_t i = 0; i < n; i++) {
        if (targets[i].pidfd >= 0 && (targets[i].state == 'T' || targets[i].state == 't')) {
            pidfd_send_signal(targets[i].pidfd, SIGCONT, NULL, 0);
        }
    }
    report->terminated = wait_for_exit(targets, fds, n, opts->grace_ms);

    signal_all(targets, n, SIGKILL, report);
    report->killed = wait_for_exit(targets, fds, n, opts->kill_wait_ms);

    for (size_t i = 0; i < n; i++) {
        if (targets[i].pidfd >= 0) {
            drop(&targets[i]);
            report->survivors++;
        }
    }

out:
    if (proc_fd >= 0) {
        close(proc_fd);
    }
    free(fds);
    free(targets);
    free(seen);
    free(order);
    return err;
}

int vly_kill(const vly_snapshot *snap, int32_t index, const vly_kill_options *opts,
             vly_kill_report *report)
{
    return vly_kill_many(snap, &index, 1, false, opts, report);
}

int vly_kill_tree(const vly_snapshot *snap, int32_t index, const vly_kill_options *opts,
                  vly_kill_report *report)
{
    return vly_kill_many(snap, &index, 1, true, opts, report);
}
