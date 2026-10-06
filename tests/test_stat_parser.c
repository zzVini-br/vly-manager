#include <errno.h>
#include <string.h>

#include "test.h"
#include "vly/process.h"

static void test_regular_process(void)
{
    vly_process p;
    const char *line = "4242 (steam) S 1 4242 4242 0 -1 4194560 100 0 0 0 "
                       "1500 250 0 0 20 -5 63 0 98765 1048576 2048 18446744073709551615\n";

    CHECK(vly_parse_stat(line, &p) == 0);
    CHECK(p.pid == 4242);
    CHECK(strcmp(p.name, "steam") == 0);
    CHECK(p.state == 'S');
    CHECK(p.ppid == 1);
    CHECK(p.utime == 1500);
    CHECK(p.stime == 250);
    CHECK(p.nice == -5);
    CHECK(p.num_threads == 63);
    CHECK(p.start_time == 98765);
    CHECK(p.rss_pages == 2048);
    CHECK(!p.is_kernel_thread);
}

static void test_name_with_spaces_and_parens(void)
{
    vly_process p;
    const char *line = "77 (a) (b c) Z 76 0 0 0 -1 0 0 0 0 0 1 2 0 0 20 0 1 0 5 0 0";

    CHECK(vly_parse_stat(line, &p) == 0);
    CHECK(strcmp(p.name, "a) (b c") == 0);
    CHECK(p.state == 'Z');
    CHECK(p.ppid == 76);
}

static void test_kernel_thread(void)
{
    vly_process p;
    const char *line = "50 (kworker/u16:3-events_unbound) I 2 0 0 0 -1 69238880 0 0 0 0 "
                       "0 9 0 0 20 0 1 0 40 0 0";

    CHECK(vly_parse_stat(line, &p) == 0);
    CHECK(p.is_kernel_thread);
    CHECK(strcmp(p.name, "kworker/u16:3-events_unbound") == 0);
}

static void test_long_name_is_truncated(void)
{
    char line[256];
    char name[100];
    memset(name, 'x', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    snprintf(line, sizeof(line), "9 (%s) R 1 0 0 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 1 0 0", name);

    vly_process p;
    CHECK(vly_parse_stat(line, &p) == 0);
    CHECK(strlen(p.name) == VLY_NAME_SIZE - 1);
    CHECK(p.state == 'R');
}

static void test_malformed_lines(void)
{
    vly_process p;

    CHECK(vly_parse_stat("", &p) == -EINVAL);
    CHECK(vly_parse_stat("12", &p) == -EINVAL);
    CHECK(vly_parse_stat("12 (name S 1 0 0 0", &p) == -EINVAL);
    CHECK(vly_parse_stat("12 (name) S 1 0 0", &p) == -EINVAL); /* truncated */
    CHECK(vly_parse_stat("x (name) S 1 0 0 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 1 0 0", &p) ==
          -EINVAL);
    CHECK(vly_parse_stat("12 (name) S -1 0 0 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 1 0 0", &p) ==
          -EINVAL);
    CHECK(vly_parse_stat("12 (name) S 1 0 0 0 -1 0 0 0 0 0 1x 0 0 0 20 0 1 0 1 0 0", &p) ==
          -EINVAL);
}

int main(void)
{
    test_regular_process();
    test_name_with_spaces_and_parens();
    test_kernel_thread();
    test_long_name_is_truncated();
    test_malformed_lines();
    return TEST_RESULT();
}
