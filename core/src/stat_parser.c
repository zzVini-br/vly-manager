#include "vly/process.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* PF_KTHREAD from include/linux/sched.h. */
#define VLY_PF_KTHREAD 0x00200000ULL

/* Field numbers as documented in proc_pid_stat(5). */
enum {
    FIELD_STATE = 3,
    FIELD_PPID = 4,
    FIELD_FLAGS = 9,
    FIELD_UTIME = 14,
    FIELD_STIME = 15,
    FIELD_NICE = 19,
    FIELD_NUM_THREADS = 20,
    FIELD_START_TIME = 22,
    FIELD_RSS = 24,
    FIELD_LAST_NEEDED = FIELD_RSS,
};

static const char *skip_spaces(const char *p)
{
    while (*p == ' ') {
        p++;
    }
    return p;
}

static bool is_token_end(char c)
{
    return c == ' ' || c == '\n' || c == '\0';
}

static int read_ll(const char **cursor, long long *out)
{
    const char *start = skip_spaces(*cursor);
    char *end;

    errno = 0;
    long long value = strtoll(start, &end, 10);
    if (end == start || errno != 0 || !is_token_end(*end)) {
        return -EINVAL;
    }

    *out = value;
    *cursor = end;
    return 0;
}

static int read_ull(const char **cursor, unsigned long long *out)
{
    const char *start = skip_spaces(*cursor);
    char *end;

    /* strtoull silently accepts and negates a leading '-'. */
    if (*start == '-') {
        return -EINVAL;
    }

    errno = 0;
    unsigned long long value = strtoull(start, &end, 10);
    if (end == start || errno != 0 || !is_token_end(*end)) {
        return -EINVAL;
    }

    *out = value;
    *cursor = end;
    return 0;
}

static int skip_token(const char **cursor)
{
    const char *p = skip_spaces(*cursor);
    if (is_token_end(*p)) {
        return -EINVAL;
    }

    while (!is_token_end(*p)) {
        p++;
    }

    *cursor = p;
    return 0;
}

static int read_pid(const char **cursor, pid_t *out)
{
    long long value;
    int err = read_ll(cursor, &value);
    if (err != 0) {
        return err;
    }
    if (value < 0 || value > INT_MAX) {
        return -EINVAL;
    }

    *out = (pid_t)value;
    return 0;
}

/* Parses the fields that follow the closing parenthesis of the name. */
static int parse_fields(const char *p, vly_process *out)
{
    unsigned long long flags = 0;
    unsigned long long num_threads = 0;
    long long nice = 0;
    int err = 0;

    for (int field = FIELD_STATE; field <= FIELD_LAST_NEEDED && err == 0; field++) {
        switch (field) {
        case FIELD_STATE:
            p = skip_spaces(p);
            if (is_token_end(*p) || !is_token_end(p[1])) {
                return -EINVAL;
            }
            out->state = *p++;
            break;
        case FIELD_PPID:
            err = read_pid(&p, &out->ppid);
            break;
        case FIELD_FLAGS:
            err = read_ull(&p, &flags);
            break;
        case FIELD_UTIME:
            err = read_ull(&p, &out->utime);
            break;
        case FIELD_STIME:
            err = read_ull(&p, &out->stime);
            break;
        case FIELD_NICE:
            err = read_ll(&p, &nice);
            break;
        case FIELD_NUM_THREADS:
            err = read_ull(&p, &num_threads);
            break;
        case FIELD_START_TIME:
            err = read_ull(&p, &out->start_time);
            break;
        case FIELD_RSS:
            err = read_ull(&p, &out->rss_pages);
            break;
        default:
            err = skip_token(&p);
            break;
        }
    }

    if (err != 0) {
        return err;
    }
    if (nice < -20 || nice > 19 || num_threads > ULONG_MAX) {
        return -EINVAL;
    }

    out->nice = (long)nice;
    out->num_threads = (unsigned long)num_threads;
    out->is_kernel_thread = (flags & VLY_PF_KTHREAD) != 0;
    return 0;
}

int vly_parse_stat(const char *line, vly_process *out)
{
    memset(out, 0, sizeof(*out));

    const char *cursor = line;
    int err = read_pid(&cursor, &out->pid);
    if (err != 0) {
        return err;
    }

    /* The name may itself contain spaces and parentheses, so it spans from
     * the first '(' to the last ')'. */
    const char *open = skip_spaces(cursor);
    const char *close = strrchr(line, ')');
    if (*open != '(' || close == NULL || close < open) {
        return -EINVAL;
    }

    size_t name_len = (size_t)(close - open - 1);
    if (name_len >= VLY_NAME_SIZE) {
        name_len = VLY_NAME_SIZE - 1;
    }
    memcpy(out->name, open + 1, name_len);
    out->name[name_len] = '\0';

    return parse_fields(close + 1, out);
}
