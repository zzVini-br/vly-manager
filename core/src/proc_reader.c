#include "proc_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A stat line is a few hundred bytes; everything vly needs (up to the rss
 * field) fits well within this even with the longest process name. */
#define STAT_BUFFER_SIZE 1024

/* The Uid line comes within the first few hundred bytes of PID/status. */
#define STATUS_BUFFER_SIZE 1024

/* Reads the effective uid from the "Uid:" line of PID/status. */
static int read_status_euid(int proc_fd, pid_t pid, uid_t *out)
{
    char path[32];
    snprintf(path, sizeof(path), "%d/status", (int)pid);

    int fd = openat(proc_fd, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }

    char buf[STATUS_BUFFER_SIZE];
    ssize_t len = read(fd, buf, sizeof(buf) - 1);
    int err = len < 0 ? -errno : 0;
    close(fd);
    if (err != 0) {
        return err;
    }
    buf[len] = '\0';

    /* "Uid:\t<real>\t<effective>\t<saved>\t<filesystem>" */
    const char *line = strstr(buf, "\nUid:");
    if (line == NULL) {
        return -ENODATA;
    }

    char *end;
    (void)strtoul(line + 5, &end, 10); /* skip the real uid */
    const char *effective = end;
    unsigned long value = strtoul(effective, &end, 10);
    if (end == effective) {
        return -ENODATA;
    }

    *out = (uid_t)value;
    return 0;
}

int vly_read_process_at(int proc_fd, pid_t pid, vly_process *out)
{
    char path[32];
    snprintf(path, sizeof(path), "%d/stat", (int)pid);

    int fd = openat(proc_fd, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }

    /* The owner of /proc/PID/stat is normally the process's effective uid,
     * which costs no extra read. */
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

    /* Non-dumpable processes (ssh-agent, polkit agents, setuid programs)
     * have their /proc files owned by root whoever runs them, so a root
     * owner is confirmed against PID/status, the source `ps` uses. Kernel
     * threads really are root. Should that read fail, the stat owner stays. */
    if (out->uid == 0 && !out->is_kernel_thread) {
        read_status_euid(proc_fd, pid, &out->uid);
    }
    return 0;
}
