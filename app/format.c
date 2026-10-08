#include "format.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USER_CACHE_SIZE 64
#define USER_NAME_SIZE 33

bool fmt_use_color(void)
{
    static int cached = -1;

    if (cached < 0) {
        cached = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
    }
    return cached != 0;
}

const char *fmt_style(const char *style)
{
    return fmt_use_color() ? style : "";
}

void fmt_memory(unsigned long long bytes, char *buf, size_t size)
{
    static const char units[] = {'B', 'K', 'M', 'G', 'T'};
    double value = (double)bytes;
    size_t unit = 0;

    while (value >= 1024.0 && unit < sizeof(units) - 1) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, size, "%lluB", bytes);
    } else if (value < 10.0) {
        snprintf(buf, size, "%.1f%c", value, units[unit]);
    } else {
        snprintf(buf, size, "%.0f%c", value, units[unit]);
    }
}

typedef struct user_entry {
    uid_t uid;
    char name[USER_NAME_SIZE];
} user_entry;

const char *fmt_user(uid_t uid)
{
    /* A handful of distinct users is typical; once full, the cache keeps
     * serving known users and new ones are looked up every time. */
    static user_entry cache[USER_CACHE_SIZE];
    static size_t cached;
    static char fallback[USER_NAME_SIZE];

    for (size_t i = 0; i < cached; i++) {
        if (cache[i].uid == uid) {
            return cache[i].name;
        }
    }

    user_entry *entry = cached < USER_CACHE_SIZE ? &cache[cached] : NULL;
    char *name = entry != NULL ? entry->name : fallback;

    struct passwd pwd;
    struct passwd *found = NULL;
    char buf[1024];
    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &found) == 0 && found != NULL) {
        snprintf(name, USER_NAME_SIZE, "%s", found->pw_name);
    } else {
        snprintf(name, USER_NAME_SIZE, "%u", (unsigned)uid);
    }

    if (entry != NULL) {
        entry->uid = uid;
        cached++;
    }
    return name;
}
