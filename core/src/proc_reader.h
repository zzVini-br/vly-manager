#ifndef VLY_PROC_READER_H
#define VLY_PROC_READER_H

/* Internal to libvly: not part of the public headers. */

#include <sys/types.h>

#include "vly/process.h"

/*
 * Reads and parses PID/stat relative to `proc_fd` (an open /proc directory)
 * and fills in the owner uid. Returns 0 or a negative errno value; -ENOENT
 * or -ESRCH mean the process is gone.
 */
int vly_read_process_at(int proc_fd, pid_t pid, vly_process *out);

#endif /* VLY_PROC_READER_H */
