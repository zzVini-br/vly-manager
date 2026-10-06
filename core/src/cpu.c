#include "vly/snapshot.h"

static void reset_cpu(vly_snapshot *snap)
{
    for (size_t i = 0; i < snap->count; i++) {
        snap->procs[i].cpu_percent = 0.0;
    }
}

void vly_snapshot_compute_cpu(vly_snapshot *cur, const vly_snapshot *prev, long ticks_per_second,
                              long cpu_count)
{
    reset_cpu(cur);

    if (prev == NULL || ticks_per_second <= 0 || cpu_count <= 0 ||
        cur->timestamp_ns <= prev->timestamp_ns) {
        return;
    }

    double elapsed_s = (double)(cur->timestamp_ns - prev->timestamp_ns) / 1e9;
    double available_ticks = elapsed_s * (double)ticks_per_second * (double)cpu_count;

    /* Both arrays are sorted by pid: walk them together like a merge. */
    size_t j = 0;
    for (size_t i = 0; i < cur->count; i++) {
        vly_process *now = &cur->procs[i];

        while (j < prev->count && prev->procs[j].pid < now->pid) {
            j++;
        }
        if (j == prev->count) {
            break;
        }

        const vly_process *before = &prev->procs[j];
        if (before->pid != now->pid || before->start_time != now->start_time) {
            continue;
        }

        unsigned long long used_now = now->utime + now->stime;
        unsigned long long used_before = before->utime + before->stime;
        if (used_now <= used_before) {
            continue;
        }

        double percent = (double)(used_now - used_before) / available_ticks * 100.0;
        now->cpu_percent = percent > 100.0 ? 100.0 : percent;
    }
}
