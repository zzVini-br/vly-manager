#include <string.h>

#include "test.h"
#include "quickfind.h"

static void test_builds_text_quickly_typed(void)
{
    quickfind qf;
    quickfind_reset(&qf);

    quickfind_step step = quickfind_feed(&qf, 's', 1000);
    CHECK(strcmp(step.text, "s") == 0);
    CHECK(step.from_next); /* a new search moves past the selection */

    step = quickfind_feed(&qf, 'T', 1100);
    CHECK(strcmp(step.text, "st") == 0); /* lowercased */
    CHECK(!step.from_next);              /* refining may keep the current match */

    step = quickfind_feed(&qf, 'e', 1200);
    CHECK(strcmp(step.text, "ste") == 0);
}

static void test_pause_starts_new_search(void)
{
    quickfind qf;
    quickfind_reset(&qf);

    quickfind_feed(&qf, 's', 1000);
    CHECK(quickfind_active(&qf, 1000 + QUICKFIND_TIMEOUT_MS));
    CHECK(!quickfind_active(&qf, 1000 + QUICKFIND_TIMEOUT_MS + 1));

    quickfind_step step = quickfind_feed(&qf, 'r', 1000 + QUICKFIND_TIMEOUT_MS + 1);
    CHECK(strcmp(step.text, "r") == 0);
    CHECK(step.from_next);
}

static void test_repeated_letter_cycles(void)
{
    quickfind qf;
    quickfind_reset(&qf);

    quickfind_feed(&qf, 's', 1000);
    for (int i = 1; i <= 3; i++) {
        quickfind_step step = quickfind_feed(&qf, 'S', 1000 + (uint64_t)i * 100);
        CHECK(strcmp(step.text, "s") == 0);
        CHECK(step.from_next);
    }

    /* A different letter after the cycling refines from there. */
    quickfind_step step = quickfind_feed(&qf, 't', 1500);
    CHECK(strcmp(step.text, "st") == 0);
    CHECK(!step.from_next);
}

static void test_text_is_bounded(void)
{
    quickfind qf;
    quickfind_reset(&qf);

    for (int i = 0; i < 100; i++) {
        quickfind_feed(&qf, (char)('a' + i % 2), 1000 + (uint64_t)i);
    }
    CHECK(qf.len == sizeof(qf.text) - 1);
    CHECK(strlen(qf.text) == qf.len);
}

int main(void)
{
    test_builds_text_quickly_typed();
    test_pause_starts_new_search();
    test_repeated_letter_cycles();
    test_text_is_bounded();
    return TEST_RESULT();
}
