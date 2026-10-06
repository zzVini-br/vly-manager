#ifndef VLY_PROCESS_H
#define VLY_PROCESS_H

#include <stdbool.h>
#include <sys/types.h>

/* Room for the longest name the kernel reports in /proc/PID/stat (kworker names
 * can exceed the 16-byte TASK_COMM_LEN), plus the terminating NUL. */
#define VLY_NAME_SIZE 64

typedef struct vly_process {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    char state; /* R, S, D, Z, T, t, I, ... as in proc_pid_stat(5) */
    bool is_kernel_thread;
    char name[VLY_NAME_SIZE];

    long nice;
    unsigned long num_threads;
    unsigned long long utime;      /* clock ticks spent in user mode */
    unsigned long long stime;      /* clock ticks spent in kernel mode */
    unsigned long long start_time; /* clock ticks after boot */
    unsigned long long rss_pages;  /* resident set size, in pages */
} vly_process;

/*
 * Parses one NUL-terminated line of /proc/PID/stat into `out`.
 * Fields not present in the stat file (such as uid) are zeroed.
 * Returns 0 on success or -EINVAL if the line is malformed.
 */
int vly_parse_stat(const char *line, vly_process *out);

#endif /* VLY_PROCESS_H */
