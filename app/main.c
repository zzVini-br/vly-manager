#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "vly/version.h"

typedef struct command {
    const char *name;
    int (*run)(int argc, char **argv);
    const char *summary;
} command;

static const command commands[] = {
    {"list", cmd_list, "Show the process tree with CPU and memory usage"},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void print_usage(FILE *out)
{
    fprintf(out, "Usage: vly COMMAND [OPTIONS]\n"
                 "\n"
                 "Commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        fprintf(out, "  %-11s %s\n", commands[i].name, commands[i].summary);
    }
    fprintf(out, "\n"
                 "Options:\n"
                 "  --version   Print version and exit\n"
                 "  --help      Show this help\n"
                 "\n"
                 "Run 'vly COMMAND --help' for details on a command.\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0) {
        printf("vly %s\n", vly_version());
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(stdout);
        return 0;
    }

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            return commands[i].run(argc - 1, argv + 1);
        }
    }

    fprintf(stderr, "vly: unknown command '%s'\n\n", argv[1]);
    print_usage(stderr);
    return 1;
}
