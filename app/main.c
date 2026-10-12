#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "commands.h"
#include "tui/tui.h"
#include "vly/version.h"

typedef struct command {
    const char *name;
    int (*run)(int argc, char **argv);
    const char *summary;
} command;

static const command commands[] = {
    {"list", cmd_list, "Show the process tree with CPU and memory usage"},
    {"kill", cmd_kill, "Stop processes by pid or name (SIGTERM, then SIGKILL)"},
    {"kill-tree", cmd_kill_tree, "Stop processes and all their descendants"},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void print_usage(FILE *out)
{
    fprintf(out, "Usage: vly [--no-theme]\n"
                 "       vly COMMAND [OPTIONS]\n"
                 "\n"
                 "Without a command, vly opens the interactive process manager.\n"
                 "\n"
                 "Commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        fprintf(out, "  %-11s %s\n", commands[i].name, commands[i].summary);
    }
    fprintf(out, "\n"
                 "Options:\n"
                 "  --no-theme  Keep the terminal's own background in the interactive view\n"
                 "  --version   Print version and exit\n"
                 "  --help      Show this help\n"
                 "\n"
                 "Run 'vly COMMAND --help' for details on a command.\n");
}

/* Killing a tree takes one pidfd per process, and the default soft limit
 * (often 1024) is easy to exceed. vly only uses poll(), never select(), so
 * raising the soft limit to the hard limit is safe. */
static void raise_open_file_limit(void)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        setrlimit(RLIMIT_NOFILE, &limit);
    }
}

int main(int argc, char **argv)
{
    raise_open_file_limit();

    bool no_theme = argc == 2 && strcmp(argv[1], "--no-theme") == 0;
    if (argc < 2 || no_theme) {
        /* The interactive view needs a terminal on both ends. */
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            print_usage(stderr);
            return 1;
        }
        tui_options opts = {.own_background = !no_theme};
        return tui_run(&opts);
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
