#ifndef VLY_APP_MATCH_H
#define VLY_APP_MATCH_H

#include <stdbool.h>
#include <sys/types.h>

/* Case-insensitive exact match. The kernel truncates process names to 15
 * characters, so a longer `wanted` matches on its first 15 characters. */
bool match_name_equals(const char *proc_name, const char *wanted);

/* Case-insensitive substring match. */
bool match_name_contains(const char *proc_name, const char *needle);

/* Parses a positive decimal pid; rejects anything else. */
bool match_parse_pid(const char *text, pid_t *out);

#endif /* VLY_APP_MATCH_H */
