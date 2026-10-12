#ifndef VLY_QUICKFIND_H
#define VLY_QUICKFIND_H

/*
 * Type-to-find, as in file managers: characters typed in quick succession
 * build a search text ("ste" finds "steam"), a pause starts a new one, and
 * repeating a single letter cycles through the names that start with it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A pause longer than this starts a new search. */
#define QUICKFIND_TIMEOUT_MS 1000

typedef struct quickfind {
    char text[32]; /* lowercase */
    size_t len;
    uint64_t last_ms;
} quickfind;

typedef struct quickfind_step {
    const char *text; /* what to search for */
    bool from_next;   /* start after the current row instead of at it */
} quickfind_step;

void quickfind_reset(quickfind *qf);

/* Feeds one typed character at `now_ms` and says how to search. */
quickfind_step quickfind_feed(quickfind *qf, char c, uint64_t now_ms);

/* True while the current search text is still being typed. */
bool quickfind_active(const quickfind *qf, uint64_t now_ms);

#endif /* VLY_QUICKFIND_H */
