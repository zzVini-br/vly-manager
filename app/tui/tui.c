#include "tui.h"

#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "format.h"
#include "theme.h"
#include "vly/snapshot.h"
#include "vly/version.h"

#define REFRESH_MS 1000
#define INPUT_POLL_MS 100

/* Rows taken by the header bar, column titles and footer bar. */
#define HEADER_ROWS 2
#define FOOTER_ROWS 1

typedef struct tui {
    vly_snapshot snapshots[2];
    vly_snapshot *cur;
    vly_snapshot *prev;
    uint64_t next_refresh_ms;
    long ticks_per_second;
    long cpu_count;
    unsigned long long page_size;
    int collect_error; /* last collection error, 0 if fine */
} tui;

static volatile sig_atomic_t quit_requested;

static void request_quit(int sig)
{
    (void)sig;
    quit_requested = 1;
}

/* Ctrl+C, a closed terminal or a kill must still restore the terminal, so
 * these signals end the main loop instead of the process. */
static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_quit;
    sigemptyset(&action.sa_mask);

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void refresh_data(tui *t)
{
    vly_snapshot *swap = t->prev;
    t->prev = t->cur;
    t->cur = swap;

    t->collect_error = vly_snapshot_collect(t->cur, "/proc");
    if (t->collect_error != 0) {
        /* Keep showing the last good snapshot. */
        swap = t->prev;
        t->prev = t->cur;
        t->cur = swap;
        return;
    }

    vly_snapshot_compute_cpu(t->cur, t->prev, t->ticks_per_second, t->cpu_count);
    vly_snapshot_build_tree(t->cur);
}

/* Fills `row` with the role's background, then writes `text` from `col`. */
static void draw_bar(int row, const char *text, theme_role role)
{
    theme_use(role, A_NORMAL);
    mvhline(row, 0, ' ', COLS);
    mvaddnstr(row, 0, text, COLS);
    theme_use(ROLE_NORMAL, A_NORMAL);
}

static void draw_header(const tui *t)
{
    char right[64];
    snprintf(right, sizeof(right), "refresh %ds ", REFRESH_MS / 1000);

    theme_use(ROLE_BAR, A_NORMAL);
    mvhline(0, 0, ' ', COLS);

    attron(A_BOLD);
    mvprintw(0, 1, "vly %s", vly_version());
    attroff(A_BOLD);
    printw("  \xc2\xb7  %zu processes", t->cur->count);

    int right_col = COLS - (int)strlen(right);
    if (right_col > 40) {
        mvaddstr(0, right_col, right);
    }

    theme_use(ROLE_DIM, A_BOLD);
    mvhline(1, 0, ' ', COLS);
    mvaddnstr(1, 0, "    PID S USER        CPU%    MEM  NAME", COLS);
    theme_use(ROLE_NORMAL, A_NORMAL);
}

static theme_role state_role(char state)
{
    switch (state) {
    case 'Z':
        return ROLE_ZOMBIE;
    case 'D':
        return ROLE_DISK_WAIT;
    default:
        return ROLE_NORMAL;
    }
}

static void draw_process(const tui *t, int row, const vly_process *proc)
{
    char mem[16];
    char name[VLY_NAME_SIZE];
    char line[256];

    fmt_memory(proc->rss_pages * t->page_size, mem, sizeof(mem));
    fmt_safe_name(proc->name, name, sizeof(name));
    snprintf(line, sizeof(line), "%7d %c %-10.10s %5.1f %6s  %s", (int)proc->pid, proc->state,
             fmt_user(proc->uid), proc->cpu_percent, mem, name);

    theme_use(state_role(proc->state), A_NORMAL);
    mvaddnstr(row, 0, line, COLS);
    theme_use(ROLE_NORMAL, A_NORMAL);
}

static void draw_body(const tui *t)
{
    int row = HEADER_ROWS;
    int last_row = LINES - FOOTER_ROWS;

    for (size_t i = 0; i < t->cur->count && row < last_row; i++) {
        const vly_process *proc = &t->cur->procs[i];
        if (!proc->is_kernel_thread) {
            draw_process(t, row++, proc);
        }
    }
}

static void draw_footer(const tui *t)
{
    char text[256];

    if (t->collect_error != 0) {
        snprintf(text, sizeof(text), " cannot read /proc: %s", strerror(-t->collect_error));
    } else {
        snprintf(text, sizeof(text), " q quit");
    }
    draw_bar(LINES - 1, text, ROLE_BAR);
}

static void draw(const tui *t)
{
    erase();
    if (LINES < HEADER_ROWS + FOOTER_ROWS + 1 || COLS < 20) {
        mvaddnstr(0, 0, "vly: window too small", COLS);
    } else {
        draw_header(t);
        draw_body(t);
        draw_footer(t);
    }
    refresh();
}

static void setup_screen(const tui_options *opts)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(INPUT_POLL_MS);

    theme_init(opts->own_background);
    bkgd(theme_attr(ROLE_NORMAL));
}

int tui_run(const tui_options *opts)
{
    tui t;
    memset(&t, 0, sizeof(t));
    vly_snapshot_init(&t.snapshots[0]);
    vly_snapshot_init(&t.snapshots[1]);
    t.cur = &t.snapshots[0];
    t.prev = &t.snapshots[1];
    t.ticks_per_second = sysconf(_SC_CLK_TCK);
    t.cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    t.page_size = (unsigned long long)sysconf(_SC_PAGESIZE);

    /* Only the character set: UTF-8 for tree lines and names. Numbers keep
     * the C locale's format, matching the command-line output. */
    setlocale(LC_CTYPE, "");
    install_signal_handlers();
    setup_screen(opts);

    while (!quit_requested) {
        uint64_t now = monotonic_ms();
        if (now >= t.next_refresh_ms) {
            refresh_data(&t);
            t.next_refresh_ms = now + REFRESH_MS;
            draw(&t);
        }

        int key = getch();
        if (key == 'q' || key == 'Q') {
            break;
        }
        if (key == KEY_RESIZE) {
            draw(&t);
        }
    }

    endwin();
    vly_snapshot_free(&t.snapshots[0]);
    vly_snapshot_free(&t.snapshots[1]);
    return 0;
}
