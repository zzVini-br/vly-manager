#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "commands.h"
#include "format.h"
#include "match.h"
#include "vly/kill.h"
#include "vly/snapshot.h"

#define MAX_GRACE_SECONDS 600

enum {
    EXIT_OK = 0,
    EXIT_USAGE_OR_NOT_FOUND = 1,
    EXIT_PARTIAL = 2,
};

static void print_usage(FILE *out, const char *command)
{
    fprintf(out,
            "Usage: vly %s [-t SECONDS] TARGET...\n"
            "\n"
            "Stops processes%s: SIGTERM first, then SIGKILL for any that are\n"
            "still running after the grace period.\n"
            "\n"
            "  TARGET              a pid, or a process name (exact, case-insensitive);\n"
            "                      every process with that name is stopped\n"
            "  -t, --timeout SECS  grace period before SIGKILL (default 5)\n"
            "  -h, --help          show this help\n",
            command, strcmp(command, "kill-tree") == 0 ? " and all their descendants" : "");
}

static bool parse_seconds(const char *text, unsigned int *out)
{
    char *end;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < 0 || value > MAX_GRACE_SECONDS) {
        return false;
    }
    *out = (unsigned int)value;
    return true;
}

/* Explains why a process can't be a kill target, or returns NULL. */
static const char *refusal_reason(const vly_process *proc)
{
    if (proc->pid == 1) {
        return "refusing to stop pid 1 (init)";
    }
    if (proc->is_kernel_thread) {
        return "kernel threads cannot be stopped";
    }
    if (proc->pid == getpid()) {
        return "refusing to stop vly itself";
    }
    return NULL;
}

static bool has_selected_ancestor(const vly_snapshot *snap, const bool *selected, int32_t index)
{
    for (int32_t p = snap->procs[index].parent; p != VLY_NO_INDEX; p = snap->procs[p].parent) {
        if (selected[p]) {
            return true;
        }
    }
    return false;
}

static size_t count_descendants(const vly_snapshot *snap, int32_t index)
{
    size_t total = 0;
    for (int32_t c = snap->procs[index].first_child; c != VLY_NO_INDEX;
         c = snap->procs[c].next_sibling) {
        total += 1 + count_descendants(snap, c);
    }
    return total;
}

static void warn_zombie(const vly_snapshot *snap, const vly_process *proc)
{
    fprintf(stderr, "vly: %s (%d) is a zombie: it already exited and only its parent can clear it",
            proc->name, (int)proc->pid);
    if (proc->parent != VLY_NO_INDEX) {
        const vly_process *parent = &snap->procs[proc->parent];
        fprintf(stderr, ".\n     Stop the parent instead: vly kill %d  (%s)\n", (int)parent->pid,
                parent->name);
    } else {
        fputs(".\n", stderr);
    }
}

/* Marks the processes named by `target` in `selected`. Returns how many
 * were marked; prints a message for targets that can't be used. */
static size_t select_target(const vly_snapshot *snap, const char *target, bool *selected)
{
    pid_t pid;

    if (match_parse_pid(target, &pid)) {
        int32_t index = vly_snapshot_index_of(snap, pid);
        if (index == VLY_NO_INDEX) {
            fprintf(stderr, "vly: no process with pid %d\n", (int)pid);
            return 0;
        }

        const vly_process *proc = &snap->procs[index];
        const char *reason = refusal_reason(proc);
        if (reason != NULL) {
            fprintf(stderr, "vly: %s\n", reason);
            return 0;
        }
        if (proc->state == 'Z') {
            warn_zombie(snap, proc);
        }
        selected[index] = true;
        return 1;
    }

    size_t found = 0;
    for (size_t i = 0; i < snap->count; i++) {
        const vly_process *proc = &snap->procs[i];
        if (match_name_equals(proc->name, target) && refusal_reason(proc) == NULL) {
            selected[i] = true;
            found++;
        }
    }

    if (found == 0) {
        fprintf(stderr, "vly: no process named '%s'\n", target);
    }
    return found;
}

static void print_plan(const vly_snapshot *snap, const int32_t *roots, size_t count,
                       bool descendants)
{
    printf("Stopping %zu %s:\n", count,
           descendants ? (count == 1 ? "process tree" : "process trees")
                       : (count == 1 ? "process" : "processes"));

    for (size_t i = 0; i < count; i++) {
        const vly_process *proc = &snap->procs[roots[i]];
        printf("  %s%s%s (%d)", fmt_style(FMT_BOLD), proc->name, fmt_style(FMT_RESET),
               (int)proc->pid);
        if (descendants) {
            size_t below = count_descendants(snap, roots[i]);
            if (below > 0) {
                printf(" + %zu %s", below, below == 1 ? "descendant" : "descendants");
            }
        }
        putchar('\n');
    }
    fflush(stdout);
}

static int print_report(const vly_kill_report *report)
{
    size_t stopped = report->terminated + report->killed;
    printf("Done: %zu stopped", stopped);
    if (stopped > 0) {
        printf(" (%zu on SIGTERM, %zu needed SIGKILL)", report->terminated, report->killed);
    }
    if (report->vanished > 0) {
        printf(", %zu had already exited", report->vanished);
    }
    printf(".\n");

    int status = EXIT_OK;
    if (report->denied > 0) {
        fprintf(stderr,
                "%svly: %zu %s belong to another user; run with sudo to stop them.%s\n",
                fmt_style(FMT_YELLOW), report->denied,
                report->denied == 1 ? "process" : "processes", fmt_style(FMT_RESET));
        status = EXIT_PARTIAL;
    }
    if (report->survivors > 0) {
        fprintf(stderr,
                "%svly: %zu %s did not exit even after SIGKILL, most likely stuck in\n"
                "     uninterruptible I/O (state D). They will go once the I/O completes.%s\n",
                fmt_style(FMT_RED), report->survivors,
                report->survivors == 1 ? "process" : "processes", fmt_style(FMT_RESET));
        status = EXIT_PARTIAL;
    }
    if (report->zombies > 0) {
        printf("%zu %s skipped: already dead, waiting for a parent to clear them.\n",
               report->zombies, report->zombies == 1 ? "zombie" : "zombies");
    }
    return status;
}

static int run(int argc, char **argv, bool descendants)
{
    vly_kill_options opts = VLY_KILL_OPTIONS_DEFAULT;
    unsigned int grace_seconds = opts.grace_ms / 1000;
    int first_target = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout, argv[0]);
            return EXIT_OK;
        }
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc || !parse_seconds(argv[i + 1], &grace_seconds)) {
                fprintf(stderr, "vly: --timeout needs a number of seconds (0-%d)\n",
                        MAX_GRACE_SECONDS);
                return EXIT_USAGE_OR_NOT_FOUND;
            }
            i++;
        } else if (strcmp(argv[i], "--") == 0) {
            first_target = i + 1;
            break;
        } else if (argv[i][0] == '-') {
            print_usage(stderr, argv[0]);
            return EXIT_USAGE_OR_NOT_FOUND;
        } else {
            first_target = i;
            break;
        }
    }

    if (first_target >= argc) {
        print_usage(stderr, argv[0]);
        return EXIT_USAGE_OR_NOT_FOUND;
    }
    opts.grace_ms = grace_seconds * 1000;

    vly_snapshot snap;
    vly_snapshot_init(&snap);
    int err = vly_snapshot_collect(&snap, "/proc");
    if (err != 0) {
        fprintf(stderr, "vly: cannot read /proc: %s\n", strerror(-err));
        return EXIT_USAGE_OR_NOT_FOUND;
    }
    vly_snapshot_build_tree(&snap);

    bool *selected = calloc(snap.count, sizeof(*selected));
    int32_t *roots = malloc(snap.count * sizeof(*roots));
    int status = EXIT_USAGE_OR_NOT_FOUND;

    if (selected == NULL || roots == NULL) {
        fputs("vly: out of memory\n", stderr);
        goto out;
    }

    bool missing = false;
    for (int i = first_target; i < argc; i++) {
        missing |= select_target(&snap, argv[i], selected) == 0;
    }

    /* In tree mode, a process inside an already selected tree is covered. */
    size_t root_count = 0;
    for (size_t i = 0; i < snap.count; i++) {
        int32_t index = (int32_t)i;
        if (selected[i] && !(descendants && has_selected_ancestor(&snap, selected, index))) {
            roots[root_count++] = index;
        }
    }

    if (root_count == 0) {
        goto out;
    }

    print_plan(&snap, roots, root_count, descendants);

    vly_kill_report report;
    err = vly_kill_many(&snap, roots, root_count, descendants, &opts, &report);
    if (err != 0) {
        fprintf(stderr, "vly: %s\n", strerror(-err));
        goto out;
    }

    status = print_report(&report);
    if (status == EXIT_OK && missing) {
        status = EXIT_PARTIAL;
    }

out:
    free(roots);
    free(selected);
    vly_snapshot_free(&snap);
    return status;
}

int cmd_kill(int argc, char **argv)
{
    return run(argc, argv, false);
}

int cmd_kill_tree(int argc, char **argv)
{
    return run(argc, argv, true);
}
