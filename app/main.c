#include <stdio.h>
#include <string.h>

#include "vly/version.h"

static void print_usage(FILE *out)
{
    fprintf(out,
            "Usage: vly [command]\n"
            "\n"
            "Commands:\n"
            "  --version    Print version and exit\n"
            "  --help       Show this help\n");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("vly %s\n", vly_version());
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }

    print_usage(stderr);
    return 1;
}
