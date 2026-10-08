#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"
#include "format.h"
#include "match.h"
#include "vly/snapshot.h"

/* Time between the two snapshots used to measure CPU usage. */
#define CPU_SAMPLE_MS 200

#define PREFIX_SIZE 512

typedef struct list_view {
    const vly_snapshot *snap;
    const char *filter; /* NULL shows everything */
    bool show_kernel;
    unsigned long long page_size;
    size_t shown;
    size_t hidden_kernel;
} list_view;

static void print_usage(FILE *out)
{
    fprintf(out, "Usage: vly list [-k] [FILTER]\n"
                 "\n"
                 "Shows the process tree with CPU and memory usage.\n"
                 "\n"
                 "  FILTER        show only processes whose name contains FILTER\n"
                 "                (case-insensitive), with all their children\n"
                 "  -k, --kernel  include kernel threads\n"
                 "  -h, --help    show this help\n");
}

static bool is_visible(const list_view *view, const vly_process *proc)
{
    return view->show_kernel || !proc->is_kernel_thread;
}

static const char *state_style(char state)
{
    switch (state) {
    case 'Z':
        return fmt_style(FMT_BOLD FMT_RED);
    case 'D':
        return fmt_style(FMT_BOLD FMT_YELLOW);
    default:
        return "";
    }
}

static void print_header(void)
{
    printf("%s%7s S %-10s %5s %6s  %s%s\n", fmt_style(FMT_BOLD), "PID", "USER", "CPU%", "MEM",
           "NAME", fmt_style(FMT_RESET));
}

static void print_row(list_view *view, const vly_process *proc, const char *prefix,
                      const char *branch)
{
    char mem[16];
    fmt_memory(proc->rss_pages * view->page_size, mem, sizeof(mem));

    const char *style = state_style(proc->state);
    const char *reset = fmt_style(FMT_RESET);
    printf("%7d %s%c%s %-10.10s %5.1f %6s  %s%s%s%s%s%s%s\n", (int)proc->pid, style, proc->state,
           reset, fmt_user(proc->uid), proc->cpu_percent, mem, fmt_style(FMT_DIM), prefix, branch,
           reset, style, proc->name, reset);
    view->shown++;
}

static int32_t next_visible(const list_view *view, int32_t index)
{
    while (index != VLY_NO_INDEX && !is_visible(view, &view->snap->procs[index])) {
        index = view->snap->procs[index].next_sibling;
    }
    return index;
}

/* Prints the children of `parent` as a tree under `prefix`. */
static void print_children(list_view *view, int32_t parent, char *prefix, size_t prefix_len)
{
    const vly_process *procs = view->snap->procs;

    for (int32_t child = next_visible(view, procs[parent].first_child); child != VLY_NO_INDEX;) {
        int32_t next = next_visible(view, procs[child].next_sibling);
        bool last = next == VLY_NO_INDEX;

        print_row(view, &procs[child], prefix, last ? "└─ " : "├─ ");

        /* Extend the prefix for this child's own children; past the buffer
         * size, deeper levels simply stop indenting. */
        const char *extension = last ? "   " : "│  ";
        size_t extension_len = strlen(extension);
        if (prefix_len + extension_len < PREFIX_SIZE) {
            memcpy(prefix + prefix_len, extension, extension_len + 1);
            print_children(view, child, prefix, prefix_len + extension_len);
            prefix[prefix_len] = '\0';
        } else {
            print_children(view, child, prefix, prefix_len);
        }

        child = next;
    }
}

static void print_subtree(list_view *view, int32_t index)
{
    char prefix[PREFIX_SIZE] = "";
    print_row(view, &view->snap->procs[index], "", "");
    print_children(view, index, prefix, 0);
}

/* Prints every subtree rooted at a process matching the filter, searching
 * below `first` (a sibling chain). Matches inside a printed subtree are
 * already shown, so the search stops there. */
static void print_matches(list_view *view, int32_t first)
{
    for (int32_t i = first; i != VLY_NO_INDEX; i = view->snap->procs[i].next_sibling) {
        const vly_process *proc = &view->snap->procs[i];
        if (!is_visible(view, proc)) {
            continue;
        }

        if (match_name_contains(proc->name, view->filter)) {
            print_subtree(view, i);
        } else {
            print_matches(view, proc->first_child);
        }
    }
}

static int collect_with_cpu(vly_snapshot *prev, vly_snapshot *cur)
{
    int err = vly_snapshot_collect(prev, "/proc");
    if (err != 0) {
        return err;
    }

    struct timespec pause = {.tv_sec = 0, .tv_nsec = CPU_SAMPLE_MS * 1000000L};
    nanosleep(&pause, NULL);

    err = vly_snapshot_collect(cur, "/proc");
    if (err != 0) {
        return err;
    }

    vly_snapshot_compute_cpu(cur, prev, sysconf(_SC_CLK_TCK), sysconf(_SC_NPROCESSORS_ONLN));
    vly_snapshot_build_tree(cur);
    return 0;
}

int cmd_list(int argc, char **argv)
{
    list_view view = {.filter = NULL, .show_kernel = false};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--kernel") == 0) {
            view.show_kernel = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (argv[i][0] == '-' || view.filter != NULL) {
            print_usage(stderr);
            return 1;
        } else {
            view.filter = argv[i];
        }
    }

    vly_snapshot prev;
    vly_snapshot cur;
    vly_snapshot_init(&prev);
    vly_snapshot_init(&cur);

    int err = collect_with_cpu(&prev, &cur);
    if (err != 0) {
        fprintf(stderr, "vly: cannot read /proc: %s\n", strerror(-err));
        vly_snapshot_free(&prev);
        vly_snapshot_free(&cur);
        return 1;
    }

    view.snap = &cur;
    view.page_size = (unsigned long long)sysconf(_SC_PAGESIZE);
    for (size_t i = 0; i < cur.count; i++) {
        view.hidden_kernel += !is_visible(&view, &cur.procs[i]);
    }

    print_header();
    if (view.filter != NULL) {
        print_matches(&view, cur.first_root);
    } else {
        for (int32_t root = next_visible(&view, cur.first_root); root != VLY_NO_INDEX;
             root = next_visible(&view, cur.procs[root].next_sibling)) {
            print_subtree(&view, root);
        }
    }

    printf("%s%zu processes shown", fmt_style(FMT_DIM), view.shown);
    if (view.hidden_kernel > 0) {
        printf(", %zu kernel threads hidden (-k to show)", view.hidden_kernel);
    }
    printf("%s\n", fmt_style(FMT_RESET));

    vly_snapshot_free(&prev);
    vly_snapshot_free(&cur);
    return 0;
}
