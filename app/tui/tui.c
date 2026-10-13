#include "tui.h"

#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "format.h"
#include "kill_jobs.h"
#include "theme.h"
#include "tree_view.h"
#include "quickfind.h"
#include "threads_view.h"
#include "vly/snapshot.h"
#include "vly/version.h"

#define REFRESH_MS 1000
#define INPUT_POLL_MS 100

/* How long the outcome of an action stays in the footer. */
#define MESSAGE_MS 5000

#define KEY_ESCAPE 27
#define KEY_TAB '\t'
#define KEY_ASCII_BACKSPACE 8
#define KEY_ASCII_DELETE 127

/* How long curses waits after Esc for the rest of an escape sequence (arrow
 * keys arrive as one). Its default of a full second makes Esc feel stuck;
 * terminal sequences arrive within a few milliseconds. */
#define ESCAPE_DELAY_MS 25

/* Rows taken by the header bar, column titles and footer bar. */
#define HEADER_ROWS 2
#define FOOTER_ROWS 1

/* What the confirmation box is asking about, if anything. */
typedef enum confirm_kind {
    CONFIRM_NONE,
    CONFIRM_TREE,    /* Shift+Del: a process and its descendants */
    CONFIRM_PROCESS, /* Del on a thread: its whole process */
} confirm_kind;

typedef struct tui {
    vly_snapshot snapshots[2];
    vly_snapshot *cur;
    vly_snapshot *prev;
    tree_view view;
    quickfind find;
    bool find_failed;    /* the text being typed matches no visible process */
    bool editing_filter; /* keys go to the filter box in the footer */
    size_t process_count; /* user-space processes in `cur` */

    kill_jobs jobs;
    threads_view threads; /* open: the list shows one process's threads */
    bool showing_help;

    confirm_kind confirm;
    proc_key confirm_key;            /* the process the box asks about */
    char confirm_name[VLY_NAME_SIZE]; /* printable */
    size_t confirm_descendants;

    char message[192]; /* outcome of the last action, shown in the footer */
    bool message_is_alert;
    uint64_t message_until_ms;

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

static void set_message(tui *t, bool alert, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(t->message, sizeof(t->message), format, args);
    va_end(args);

    t->message_is_alert = alert;
    t->message_until_ms = monotonic_ms() + MESSAGE_MS;
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

    if (t->threads.open) {
        err = threads_view_refresh(&t->threads, "/proc", t->ticks_per_second, t->cpu_count);
        if (err == -ESRCH) {
            char name[VLY_NAME_SIZE];
            fmt_safe_name(t->threads.owner_name, name, sizeof(name));
            threads_view_close(&t->threads);
            set_message(t, false, "%s exited", name);
        } else if (err != 0) {
            t->error = err;
        }
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
    if (t->threads.open) {
        char name[VLY_NAME_SIZE];
        fmt_safe_name(t->threads.owner_name, name, sizeof(name));
        snprintf(left, sizeof(left), "  \xc2\xb7  %s (%d)  \xc2\xb7  %zu threads", name,
                 (int)t->threads.owner.pid, t->threads.cur->count);
    } else if (t->view.filter[0] != '\0') {
        snprintf(left, sizeof(left), "  \xc2\xb7  %zu shown of %zu processes", t->view.row_count,
                 t->process_count);
    } else {
        snprintf(left, sizeof(left), "  \xc2\xb7  %zu processes", t->process_count);
    }
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
    /* The sorted column is marked with ▾ in place of the space before it. */
    const char *columns = "    PID S USER        CPU%    MEM  NAME";
    if (t->threads.open) {
        columns = "    TID S  CPU%  THREAD";
    } else if (t->view.mode == TREE_VIEW_BY_CPU) {
        columns = "    PID S USER       \xe2\x96\xbe" "CPU%    MEM  NAME";
    } else if (t->view.mode == TREE_VIEW_BY_MEMORY) {
        columns = "    PID S USER        CPU%   \xe2\x96\xbe" "MEM  NAME";
    }
    put_clipped(&col, columns);
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
    bool stopping = kill_jobs_is_target(&t->jobs, proc);
    if (stopping) {
        snprintf(hidden, sizeof(hidden), "  stopping\xe2\x80\xa6");
    } else if (tree_view_is_collapsed(&t->view, row->index)) {
        snprintf(hidden, sizeof(hidden), "  +%zu", tree_view_hidden_below(&t->view, row->index));
    }

    theme_role base = selected ? ROLE_SELECTED : ROLE_NORMAL;
    theme_role state = selected   ? ROLE_SELECTED
                       : stopping ? ROLE_DIM
                                  : state_role(proc->state);
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

static void draw_thread_row(int screen_row, const vly_process *thread, bool selected)
{
    char columns[32];
    char name[VLY_NAME_SIZE];
    fmt_safe_name(thread->name, name, sizeof(name));

    theme_role base = selected ? ROLE_SELECTED : ROLE_NORMAL;
    theme_role state = selected ? ROLE_SELECTED : state_role(thread->state);

    theme_use(base, A_NORMAL);
    mvhline(screen_row, 0, ' ', COLS);
    move(screen_row, 0);

    int col = 0;
    snprintf(columns, sizeof(columns), "%7d ", (int)thread->pid);
    put_clipped(&col, columns);
    char state_text[2] = {thread->state, '\0'};
    theme_use(state, A_NORMAL);
    put_clipped(&col, state_text);
    snprintf(columns, sizeof(columns), " %5.1f  ", thread->cpu_percent);
    theme_use(base, A_NORMAL);
    put_clipped(&col, columns);
    theme_use(state, A_NORMAL);
    put_clipped(&col, name);
}

static void draw_threads(tui *t)
{
    size_t height = body_height();
    threads_view_scroll_to_cursor(&t->threads, height);

    const vly_snapshot *threads = t->threads.cur;
    for (size_t i = 0; i < height && t->threads.scroll + i < threads->count; i++) {
        size_t row = t->threads.scroll + i;
        draw_thread_row(HEADER_ROWS + (int)i, &threads->procs[row], row == t->threads.cursor);
    }
}

static void draw_body(tui *t)
{
    if (t->threads.open) {
        draw_threads(t);
        return;
    }

    size_t height = body_height();
    tree_view_scroll_to_cursor(&t->view, height);

    if (t->view.row_count == 0 && t->view.filter[0] != '\0' && height > 1) {
        char text[128];
        snprintf(text, sizeof(text), "no process name contains \"%s\"", t->view.filter);
        theme_use(ROLE_DIM, A_NORMAL);
        int col = 2;
        move(HEADER_ROWS + 1, col);
        put_clipped(&col, text);
        return;
    }

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
    } else if (t->confirm != CONFIRM_NONE) {
        snprintf(text, sizeof(text), " Enter stop   Esc cancel");
    } else if (monotonic_ms() < t->message_until_ms) {
        snprintf(text, sizeof(text), " %s%s", t->message_is_alert ? "! " : "", t->message);
    } else if (t->threads.open) {
        snprintf(text, sizeof(text),
                 " \xe2\x86\x91\xe2\x86\x93 move   Del stop process   Enter/Esc back   ? help   F10 quit");
    } else if (t->editing_filter) {
        snprintf(text, sizeof(text), " filter: %s_   Enter keep   Esc clear", t->view.filter);
    } else if (t->view.filter[0] != '\0') {
        snprintf(text, sizeof(text), " filter: %s   / edit   Esc clear   F10 quit",
                 t->view.filter);
    } else if (quickfind_active(&t->find, monotonic_ms())) {
        snprintf(text, sizeof(text), " find: %s%s", t->find.text,
                 t->find_failed ? "   (no match)" : "");
    } else {
        snprintf(text, sizeof(text),
                 " Del stop  Shift+Del tree  Enter threads  / filter  Tab sort  ? help  Esc quit");
    }

    bool alert = t->error != 0 || (t->confirm == CONFIRM_NONE && monotonic_ms() < t->message_until_ms &&
                                   t->message_is_alert);
    theme_use(ROLE_BAR, alert ? A_BOLD : A_NORMAL);
    mvhline(LINES - 1, 0, ' ', COLS);
    int col = 0;
    move(LINES - 1, 0);
    put_clipped(&col, text);
}

/* The confirmation: a plain box over the middle of the list. */
static void draw_confirm(const tui *t)
{
    const char *title;
    char lines[2][160] = {"", ""};

    if (t->confirm == CONFIRM_TREE) {
        title = "Stop process tree?";
        snprintf(lines[0], sizeof(lines[0]), "%s (%d) and its %zu %s", t->confirm_name,
                 (int)t->confirm_key.pid, t->confirm_descendants,
                 t->confirm_descendants == 1 ? "descendant" : "descendants");
    } else {
        title = "Stop the whole process?";
        snprintf(lines[0], sizeof(lines[0]), "Linux cannot stop a single thread.");
        snprintf(lines[1], sizeof(lines[1]), "%s (%d) stops with all its threads.",
                 t->confirm_name, (int)t->confirm_key.pid);
    }

    int width = 34;
    for (int i = 0; i < 2; i++) {
        int needed = (int)strlen(lines[i]) + 6;
        width = needed > width ? needed : width;
    }
    if (width > COLS - 2) {
        width = COLS - 2;
    }
    int height = lines[1][0] != '\0' ? 6 : 5;
    int top = HEADER_ROWS + ((int)body_height() - height) / 2;
    int left = (COLS - width) / 2;
    if (top < HEADER_ROWS) {
        top = HEADER_ROWS;
    }

    theme_use(ROLE_BAR, A_NORMAL);
    for (int r = 0; r < height; r++) {
        mvhline(top + r, left, ' ', width);
    }

    int row = top + 1;
    int col = left + 3;
    move(row++, col);
    theme_use(ROLE_BAR, A_BOLD);
    put_clipped(&col, title);
    theme_use(ROLE_BAR, A_NORMAL);
    for (int i = 0; i < 2 && lines[i][0] != '\0'; i++) {
        col = left + 3;
        move(row++, col);
        put_clipped(&col, lines[i]);
    }
    col = left + 3;
    move(row, col);
    theme_use(ROLE_ACCENT, A_NORMAL);
    put_clipped(&col, "Enter stop   Esc cancel");
}

static const char *const help_lines[][2] = {
    {"\xe2\x86\x91 \xe2\x86\x93  PgUp PgDn  Home End", "move"},
    {"\xe2\x86\x90 \xe2\x86\x92", "collapse / expand, go to parent / child"},
    {"type a name", "jump to it; repeat a letter to cycle"},
    {"/", "filter by name (Esc clears it)"},
    {"Tab", "tree, sorted by CPU, sorted by memory"},
    {"Enter", "threads of the selected process"},
    {"Del", "stop the process (SIGTERM, then SIGKILL)"},
    {"Shift+Del", "stop the process and its descendants"},
    {"Esc  F10", "quit"},
};

#define HELP_LINE_COUNT (sizeof(help_lines) / sizeof(help_lines[0]))
#define HELP_KEY_WIDTH 26

static void draw_help(void)
{
    int width = 72 < COLS - 2 ? 72 : COLS - 2;
    int height = (int)HELP_LINE_COUNT + 7;
    int top = HEADER_ROWS + ((int)body_height() - height) / 2;
    int left = (COLS - width) / 2;
    if (top < 0) {
        top = 0;
    }

    theme_use(ROLE_BAR, A_NORMAL);
    for (int r = 0; r < height && top + r < LINES - 1; r++) {
        mvhline(top + r, left, ' ', width);
    }

    int row = top + 1;
    int col = left + 3;
    move(row, col);
    theme_use(ROLE_BAR, A_BOLD);
    put_clipped(&col, "Keys");
    row += 2;

    for (size_t i = 0; i < HELP_LINE_COUNT && row < LINES - 1; i++, row++) {
        col = left + 3;
        move(row, col);
        theme_use(ROLE_ACCENT, A_NORMAL);
        put_clipped(&col, help_lines[i][0]);
        col = left + 3 + HELP_KEY_WIDTH;
        move(row, col);
        theme_use(ROLE_BAR, A_NORMAL);
        put_clipped(&col, help_lines[i][1]);
    }

    row++;
    if (row < LINES - 1) {
        col = left + 3;
        move(row, col);
        theme_use(ROLE_BAR, A_NORMAL);
        put_clipped(&col, "States: ");
        theme_use(ROLE_ZOMBIE, A_NORMAL);
        put_clipped(&col, "Z");
        theme_use(ROLE_BAR, A_NORMAL);
        put_clipped(&col, " zombie, already dead   ");
        theme_use(ROLE_DISK_WAIT, A_NORMAL);
        put_clipped(&col, "D");
        theme_use(ROLE_BAR, A_NORMAL);
        put_clipped(&col, " waiting on I/O, cannot be killed");
    }
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
        if (t->confirm != CONFIRM_NONE) {
            draw_confirm(t);
        }
        if (t->showing_help) {
            draw_help();
        }
        draw_footer(t);
    }
    theme_use(ROLE_NORMAL, A_NORMAL);
    refresh();
}

/* Keys that type into the search: printable ASCII, except those kept for
 * commands. */
static bool is_find_key(int key)
{
    return key >= ' ' && key <= '~' && key != '/' && key != '?';
}

static void find(tui *t, char c)
{
    quickfind_step step = quickfind_feed(&t->find, c, monotonic_ms());
    t->find_failed = !tree_view_select_prefix(&t->view, step.text, step.from_next);
}

/* Starts stopping the process at `index` of the current snapshot. */
static void start_stop(tui *t, int32_t index, bool tree)
{
    const vly_process *proc = &t->cur->procs[index];
    char name[VLY_NAME_SIZE];
    fmt_safe_name(proc->name, name, sizeof(name));

    if (proc->state == 'Z') {
        if (proc->parent != VLY_NO_INDEX) {
            const vly_process *parent = &t->cur->procs[proc->parent];
            char parent_name[VLY_NAME_SIZE];
            fmt_safe_name(parent->name, parent_name, sizeof(parent_name));
            set_message(t, true, "%s is a zombie: stop its parent instead, %s (%d)", name,
                        parent_name, (int)parent->pid);
        } else {
            set_message(t, true, "%s is a zombie: already dead", name);
        }
        return;
    }

    int err = kill_jobs_start(&t->jobs, t->cur, index, tree, NULL);
    if (err == -EINVAL) {
        set_message(t, true, "vly never stops pid 1, kernel threads or itself");
    } else if (err == -EBUSY) {
        set_message(t, true, "%d stops already running; wait for one to finish", KILL_JOBS_MAX);
    } else if (err != 0) {
        set_message(t, true, "Could not stop %s: %s", name, strerror(-err));
    } else {
        set_message(t, false, "Stopping %s%s\xe2\x80\xa6", name, tree ? " and its descendants" : "");
    }
}

static int32_t selected_index(const tui *t)
{
    return t->view.row_count == 0 ? VLY_NO_INDEX : t->view.rows[t->view.cursor].index;
}

/* Shift+Del: confirm first when there are descendants to take along. */
static void ask_stop_tree(tui *t)
{
    int32_t index = selected_index(t);
    if (index == VLY_NO_INDEX) {
        return;
    }

    size_t descendants = tree_view_hidden_below(&t->view, index);
    if (descendants == 0) {
        start_stop(t, index, true);
        return;
    }

    const vly_process *proc = &t->cur->procs[index];
    t->confirm_key = (proc_key){.pid = proc->pid, .start_time = proc->start_time};
    fmt_safe_name(proc->name, t->confirm_name, sizeof(t->confirm_name));
    t->confirm_descendants = descendants;
    t->confirm = CONFIRM_TREE;
}

/* Del in the threads view: threads can't be stopped alone, so ask before
 * stopping the whole process. */
static void ask_stop_threads_owner(tui *t)
{
    t->confirm_key = t->threads.owner;
    fmt_safe_name(t->threads.owner_name, t->confirm_name, sizeof(t->confirm_name));
    t->confirm_descendants = 0;
    t->confirm = CONFIRM_PROCESS;
}

/* The dialog takes every key: Enter confirms, anything else cancels. */
static void handle_confirm_key(tui *t, int key)
{
    if (key == KEY_RESIZE) {
        return;
    }
    confirm_kind kind = t->confirm;
    t->confirm = CONFIRM_NONE;
    if (key != '\n' && key != '\r' && key != KEY_ENTER) {
        return;
    }

    /* The list may have refreshed while the dialog was open. */
    int32_t index = vly_snapshot_index_of(t->cur, t->confirm_key.pid);
    if (index == VLY_NO_INDEX || t->cur->procs[index].start_time != t->confirm_key.start_time) {
        set_message(t, false, "The process exited meanwhile");
        return;
    }
    if (kind == CONFIRM_PROCESS) {
        threads_view_close(&t->threads);
    }
    start_stop(t, index, kind == CONFIRM_TREE);
}

static void set_filter(tui *t, const char *text)
{
    t->error = tree_view_set_filter(&t->view, text);
}

/*
 * Keys while the filter box is open. Returns false for keys it does not
 * handle (arrows and the like): they close the box and act as usual.
 */
static bool handle_filter_key(tui *t, int key)
{
    char text[TREE_VIEW_FILTER_SIZE];
    snprintf(text, sizeof(text), "%s", t->view.filter);
    size_t len = strlen(text);

    if (key >= ' ' && key <= '~') {
        if (len + 1 < sizeof(text)) {
            text[len] = (char)key;
            text[len + 1] = '\0';
            set_filter(t, text);
        }
    } else if (key == KEY_BACKSPACE || key == KEY_ASCII_BACKSPACE || key == KEY_ASCII_DELETE) {
        if (len > 0) {
            text[len - 1] = '\0';
            set_filter(t, text);
        }
    } else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
        t->editing_filter = false;
    } else if (key == KEY_ESCAPE) {
        set_filter(t, "");
        t->editing_filter = false;
    } else if (key != KEY_RESIZE) {
        t->editing_filter = false;
        return false;
    }

    draw(t);
    return true;
}

static bool is_enter(int key)
{
    return key == '\n' || key == '\r' || key == KEY_ENTER;
}

/* Enter on a process: show its threads. */
static void open_threads(tui *t)
{
    int32_t index = selected_index(t);
    if (index == VLY_NO_INDEX) {
        return;
    }

    const vly_process *proc = &t->cur->procs[index];
    int err = threads_view_open(&t->threads, proc, "/proc");
    if (err != 0) {
        char name[VLY_NAME_SIZE];
        fmt_safe_name(proc->name, name, sizeof(name));
        set_message(t, true, "Cannot list the threads of %s: %s", name,
                    err == -ESRCH ? "it exited" : strerror(-err));
    }
}

/* Keys in the threads view. Returns false when the key ends the program. */
static bool handle_threads_key(tui *t, int key)
{
    long page = (long)body_height() - 1;
    if (page < 1) {
        page = 1;
    }

    switch (key) {
    case KEY_F(10):
        return false;
    case KEY_ESCAPE:
    case KEY_LEFT:
    case '\n':
    case '\r':
    case KEY_ENTER:
        threads_view_close(&t->threads);
        break;
    case KEY_UP:
        threads_view_move(&t->threads, -1);
        break;
    case KEY_DOWN:
        threads_view_move(&t->threads, 1);
        break;
    case KEY_PPAGE:
        threads_view_move(&t->threads, -page);
        break;
    case KEY_NPAGE:
        threads_view_move(&t->threads, page);
        break;
    case KEY_HOME:
        threads_view_move(&t->threads, -(long)t->threads.cur->count);
        break;
    case KEY_END:
        threads_view_move(&t->threads, (long)t->threads.cur->count);
        break;
    case KEY_DC:
    case KEY_SDC:
        ask_stop_threads_owner(t);
        break;
    case KEY_RESIZE:
        break;
    default:
        return true;
    }

    draw(t);
    return true;
}

/* Returns false when the key should end the program. */
static bool handle_key(tui *t, int key)
{
    /* The help box takes any key to close. */
    if (t->showing_help) {
        if (key != KEY_RESIZE) {
            t->showing_help = false;
        }
        draw(t);
        return true;
    }

    if ((key == '?' && !t->editing_filter) || key == KEY_F(1)) {
        t->showing_help = true;
        draw(t);
        return true;
    }

    if (t->confirm != CONFIRM_NONE) {
        handle_confirm_key(t, key);
        draw(t);
        return true;
    }

    if (t->threads.open) {
        return handle_threads_key(t, key);
    }

    if (is_enter(key) && !t->editing_filter) {
        quickfind_reset(&t->find);
        open_threads(t);
        draw(t);
        return true;
    }

    if (t->editing_filter && handle_filter_key(t, key)) {
        return true;
    }

    if (key == '/') {
        quickfind_reset(&t->find);
        t->editing_filter = true;
        draw(t);
        return true;
    }

    if (is_find_key(key)) {
        find(t, (char)key);
        draw(t);
        return true;
    }

    /* Any other key ends the search being typed. */
    if (key != KEY_RESIZE) {
        quickfind_reset(&t->find);
    }

    long page = (long)body_height() - 1;
    if (page < 1) {
        page = 1;
    }

    switch (key) {
    case KEY_ESCAPE:
        /* Esc first clears an active filter, and quits only after that. */
        if (t->view.filter[0] == '\0') {
            return false;
        }
        set_filter(t, "");
        break;
    case KEY_F(10):
        return false;
    case KEY_TAB:
        /* Tree, then by CPU, then by memory, then the tree again. */
        t->error = tree_view_set_mode(&t->view, t->view.mode == TREE_VIEW_TREE     ? TREE_VIEW_BY_CPU
                                                : t->view.mode == TREE_VIEW_BY_CPU ? TREE_VIEW_BY_MEMORY
                                                                                   : TREE_VIEW_TREE);
        break;
    case KEY_DC:
        if (selected_index(t) != VLY_NO_INDEX) {
            start_stop(t, selected_index(t), false);
        }
        break;
    case KEY_SDC:
        ask_stop_tree(t);
        break;
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
    set_escdelay(ESCAPE_DELAY_MS);
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
    quickfind_reset(&t.find);
    kill_jobs_init(&t.jobs);
    threads_view_init(&t.threads);
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
        kill_result result;
        while (kill_jobs_reap(&t.jobs, &result)) {
            char text[sizeof(t.message)];
            bool alert = kill_result_describe(&result, text, sizeof(text));
            set_message(&t, alert, "%s", text);
            t.next_refresh_ms = 0; /* show the outcome in the list right away */
        }

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

    /* A job interrupted mid-way would never send its SIGKILL. */
    if (kill_jobs_busy(&t.jobs)) {
        set_message(&t, false, "Waiting for running stops to finish\xe2\x80\xa6");
        draw(&t);
        kill_jobs_finish_all(&t.jobs);
    }

    endwin();
    kill_jobs_finish_all(&t.jobs);
    threads_view_free(&t.threads);
    tree_view_free(&t.view);
    vly_snapshot_free(&t.snapshots[0]);
    vly_snapshot_free(&t.snapshots[1]);
    return 0;
}
