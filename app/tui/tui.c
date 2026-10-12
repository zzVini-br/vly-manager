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
#include "tree_view.h"
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
    tree_view view;
    size_t process_count; /* user-space processes in `cur` */
    uint64_t next_refresh_ms;
    long ticks_per_second;
    long cpu_count;
    unsigned long long page_size;
    int error; /* last collection or view error, 0 if fine */
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

static size_t body_height(void)
{
    int height = LINES - HEADER_ROWS - FOOTER_ROWS;
    return height > 0 ? (size_t)height : 0;
}

/*
 * Collects into the spare snapshot buffer and only then makes it current,
 * so the view never points at a buffer being overwritten.
 */
static void refresh_data(tui *t)
{
    vly_snapshot *next = t->prev;

    int err = vly_snapshot_collect(next, "/proc");
    if (err != 0) {
        t->error = err; /* keep showing the last good snapshot */
        return;
    }

    vly_snapshot_compute_cpu(next, t->cur, t->ticks_per_second, t->cpu_count);
    vly_snapshot_build_tree(next);

    t->prev = t->cur;
    t->cur = next;
    t->error = tree_view_rebuild(&t->view, t->cur);

    t->process_count = 0;
    for (size_t i = 0; i < t->cur->count; i++) {
        t->process_count += !t->cur->procs[i].is_kernel_thread;
    }
}

/* Writes `text` from the current position up to the right edge, one UTF-8
 * character (one column for everything vly draws) at a time. */
static void put_clipped(int *col, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    while (*p != '\0' && *col < COLS) {
        int len = 1;
        while ((p[len] & 0xc0) == 0x80) {
            len++;
        }
        addnstr((const char *)p, len);
        p += len;
        (*col)++;
    }
}

static void draw_header(const tui *t)
{
    char left[128];
    char right[64];
    snprintf(left, sizeof(left), "  \xc2\xb7  %zu processes", t->process_count);
    snprintf(right, sizeof(right), "refresh %ds ", REFRESH_MS / 1000);

    theme_use(ROLE_BAR, A_NORMAL);
    mvhline(0, 0, ' ', COLS);

    int col = 1;
    move(0, col);
    theme_use(ROLE_BAR, A_BOLD);
    put_clipped(&col, "vly ");
    put_clipped(&col, vly_version());
    theme_use(ROLE_BAR, A_NORMAL);
    put_clipped(&col, left);

    int right_col = COLS - (int)strlen(right);
    if (right_col > col + 2) {
        mvaddstr(0, right_col, right);
    }

    theme_use(ROLE_DIM, A_BOLD);
    mvhline(1, 0, ' ', COLS);
    col = 0;
    move(1, 0);
    put_clipped(&col, "    PID S USER        CPU%    MEM  NAME");
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

static void draw_row(const tui *t, int screen_row, const tree_row *row, bool selected)
{
    const vly_process *proc = &t->cur->procs[row->index];
    char mem[16];
    char columns[96];
    char prefix[512];
    char name[VLY_NAME_SIZE];
    char hidden[32] = "";

    fmt_memory(proc->rss_pages * t->page_size, mem, sizeof(mem));
    snprintf(columns, sizeof(columns), "%7d ", (int)proc->pid);
    tree_view_prefix(&t->view, row, prefix, sizeof(prefix));
    fmt_safe_name(proc->name, name, sizeof(name));
    if (tree_view_is_collapsed(&t->view, row->index)) {
        snprintf(hidden, sizeof(hidden), "  +%zu", tree_view_hidden_below(&t->view, row->index));
    }

    theme_role base = selected ? ROLE_SELECTED : ROLE_NORMAL;
    theme_role state = selected ? ROLE_SELECTED : state_role(proc->state);
    theme_role dim = selected ? ROLE_SELECTED : ROLE_DIM;

    theme_use(base, A_NORMAL);
    mvhline(screen_row, 0, ' ', COLS);
    move(screen_row, 0);

    int col = 0;
    put_clipped(&col, columns);
    char state_text[2] = {proc->state, '\0'};
    theme_use(state, A_NORMAL);
    put_clipped(&col, state_text);

    snprintf(columns, sizeof(columns), " %-10.10s %5.1f %6s  ", fmt_user(proc->uid),
             proc->cpu_percent, mem);
    theme_use(base, A_NORMAL);
    put_clipped(&col, columns);

    theme_use(dim, A_NORMAL);
    put_clipped(&col, prefix);
    theme_use(state, A_NORMAL);
    put_clipped(&col, name);
    theme_use(dim, A_NORMAL);
    put_clipped(&col, hidden);
}

static void draw_body(tui *t)
{
    size_t height = body_height();
    tree_view_scroll_to_cursor(&t->view, height);

    for (size_t i = 0; i < height && t->view.scroll + i < t->view.row_count; i++) {
        size_t row = t->view.scroll + i;
        draw_row(t, HEADER_ROWS + (int)i, &t->view.rows[row], row == t->view.cursor);
    }
}

static void draw_footer(const tui *t)
{
    char text[256];

    if (t->error != 0) {
        snprintf(text, sizeof(text), " vly: %s", strerror(-t->error));
    } else {
        snprintf(text, sizeof(text),
                 " \xe2\x86\x91\xe2\x86\x93 move   \xe2\x86\x90\xe2\x86\x92 collapse/expand   "
                 "q quit");
    }

    theme_use(ROLE_BAR, A_NORMAL);
    mvhline(LINES - 1, 0, ' ', COLS);
    int col = 0;
    move(LINES - 1, 0);
    put_clipped(&col, text);
}

static void draw(tui *t)
{
    erase();
    if (LINES < HEADER_ROWS + FOOTER_ROWS + 1 || COLS < 20) {
        theme_use(ROLE_NORMAL, A_NORMAL);
        mvaddnstr(0, 0, "vly: window too small", COLS);
    } else {
        draw_header(t);
        draw_body(t);
        draw_footer(t);
    }
    theme_use(ROLE_NORMAL, A_NORMAL);
    refresh();
}

/* Returns false when the key should end the program. */
static bool handle_key(tui *t, int key)
{
    long page = (long)body_height() - 1;
    if (page < 1) {
        page = 1;
    }

    switch (key) {
    case 'q':
    case 'Q':
        return false;
    case KEY_UP:
        tree_view_move(&t->view, -1);
        break;
    case KEY_DOWN:
        tree_view_move(&t->view, 1);
        break;
    case KEY_PPAGE:
        tree_view_move(&t->view, -page);
        break;
    case KEY_NPAGE:
        tree_view_move(&t->view, page);
        break;
    case KEY_HOME:
        tree_view_move_to_start(&t->view);
        break;
    case KEY_END:
        tree_view_move_to_end(&t->view);
        break;
    case KEY_LEFT:
        t->error = tree_view_left(&t->view);
        break;
    case KEY_RIGHT:
        t->error = tree_view_right(&t->view);
        break;
    case KEY_RESIZE:
        break;
    default:
        return true; /* unknown keys don't redraw */
    }

    draw(t);
    return true;
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
    tree_view_init(&t.view);
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

    bool running = true;
    while (running && !quit_requested) {
        uint64_t now = monotonic_ms();
        if (now >= t.next_refresh_ms) {
            refresh_data(&t);
            t.next_refresh_ms = now + REFRESH_MS;
            draw(&t);
        }

        int key = getch();
        if (key != ERR) {
            running = handle_key(&t, key);
        }
    }

    endwin();
    tree_view_free(&t.view);
    vly_snapshot_free(&t.snapshots[0]);
    vly_snapshot_free(&t.snapshots[1]);
    return 0;
}
