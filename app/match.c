#include "match.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

/* TASK_COMM_LEN (16) minus the terminating NUL. */
#define KERNEL_NAME_MAX 15

bool match_name_equals(const char *proc_name, const char *wanted)
{
    if (strcasecmp(proc_name, wanted) == 0) {
        return true;
    }

    return strlen(proc_name) == KERNEL_NAME_MAX && strlen(wanted) > KERNEL_NAME_MAX &&
           strncasecmp(proc_name, wanted, KERNEL_NAME_MAX) == 0;
}

bool match_name_contains(const char *proc_name, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return true;
    }

    for (const char *p = proc_name; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

bool match_parse_pid(const char *text, pid_t *out)
{
    long value = 0;

    if (*text == '\0') {
        return false;
    }

    for (const char *p = text; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        value = value * 10 + (*p - '0');
        if (value > INT_MAX) {
            return false;
        }
    }

    if (value == 0) {
        return false;
    }

    *out = (pid_t)value;
    return true;
}
