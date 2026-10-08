#include "proc_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* A stat line is a few hundred bytes; everything vly needs (up to the rss
 * field) fits well within this even with the longest process name. */
#define STAT_BUFFER_SIZE 1024

int vly_read_process_at(int proc_fd, pid_t pid, vly_process *out)
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
