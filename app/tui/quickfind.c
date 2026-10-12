#include "quickfind.h"

#include <ctype.h>

void quickfind_reset(quickfind *qf)
{
    qf->text[0] = '\0';
    qf->len = 0;
    qf->last_ms = 0;
}

bool quickfind_active(const quickfind *qf, uint64_t now_ms)
{
    return qf->len > 0 && now_ms - qf->last_ms <= QUICKFIND_TIMEOUT_MS;
}

/* True when the text is one letter typed one or more times, e.g. "sss". */
static bool repeats_only(const quickfind *qf, char c)
{
    for (size_t i = 0; i < qf->len; i++) {
        if (qf->text[i] != c) {
            return false;
        }
    }
    return true;
}

quickfind_step quickfind_feed(quickfind *qf, char c, uint64_t now_ms)
{
    c = (char)tolower((unsigned char)c);

    if (!quickfind_active(qf, now_ms)) {
        qf->len = 0;
    }
    qf->last_ms = now_ms;

    /* Repeating the same letter cycles through its matches. */
    if (qf->len > 0 && repeats_only(qf, c)) {
        qf->text[0] = c;
        qf->text[1] = '\0';
        qf->len = 1;
        return (quickfind_step){.text = qf->text, .from_next = true};
    }

    if (qf->len + 1 < sizeof(qf->text)) {
        qf->text[qf->len++] = c;
        qf->text[qf->len] = '\0';
    }

    /* A new search moves on from the selection; a longer text refines the
     * current match, which may already be the right one. */
    return (quickfind_step){.text = qf->text, .from_next = qf->len == 1};
}
