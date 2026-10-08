#ifndef VLY_APP_FORMAT_H
#define VLY_APP_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* ANSI styles, used only when fmt_use_color() is true. */
#define FMT_RESET "\033[0m"
#define FMT_BOLD "\033[1m"
#define FMT_DIM "\033[2m"
#define FMT_RED "\033[31m"
#define FMT_YELLOW "\033[33m"

/* True when stdout is a terminal and NO_COLOR is not set. */
bool fmt_use_color(void);

/* Returns `style` when colors are enabled, "" otherwise. */
const char *fmt_style(const char *style);

/* Formats a byte count as a short human-readable size, e.g. "1.4G", "512M". */
void fmt_memory(unsigned long long bytes, char *buf, size_t size);

/* Returns the user name for `uid` (cached), or the numeric uid. */
const char *fmt_user(uid_t uid);

#endif /* VLY_APP_FORMAT_H */
