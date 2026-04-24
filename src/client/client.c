/*
 * ============================================================
 *  Cloud Task Scheduler - CLIENT (TUI Edition)
 *  Subject  : Operating Systems (2nd Year B.Tech ENTC)
 *  Language : C  |  Platform : Linux / POSIX
 *
 *  System Calls Used:
 *    Network : socket, connect, send, recv, close
 *
 *  TUI      : ncurses
 *  Compile  : gcc client.c -o client -lncurses
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>       
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ncurses.h>

/* ── Settings ──────────────────────────────────────────── */
#define SERVER_IP   "127.0.0.1"
#define PORT        8080
#define NAME_LEN    64
#define SCRIPT_MAX  4096

/* ── TUI Windows — declared ONCE here ─────────────────── */
static WINDOW *win_header = NULL;
static WINDOW *win_menu   = NULL;
static WINDOW *win_form   = NULL;
static WINDOW *win_log    = NULL;
static WINDOW *win_footer = NULL;

/* ── Client-side log ───────────────────────────────────── */
#define CLOG_MAX 100
static char clog[CLOG_MAX][160];
static int  clog_count = 0;

/* ════════════════════════════════════════════════════════
   TASK TYPE DEFINITIONS
   ════════════════════════════════════════════════════════ */
typedef struct {
    int  id;
    char label[48];
    char description[80];
    int  default_priority;
} TaskType;

static const TaskType task_types[] = {
    { 1, "Compile & Run C++",
      "g++ -std=c++17 <file>.cpp -o out && ./out",                3 },
    { 2, "Run Python Script",
      "python3 <file>.py",                                        2 },
    { 3, "Backup a File",
      "cp <src> /tmp/backup_<name>_$(date +%s)",                  1 },
    { 4, "Disk Usage Report",
      "df -h > /tmp/disk_report.txt && cat /tmp/disk_report.txt", 1 },
    { 5, "Word / Line Count",
      "wc -lwc <file>",                                           1 },
    { 6, "Find & Delete .tmp Files",
      "find <dir> -name '*.tmp' -delete && echo Done",            2 },
    { 7, "Archive Directory (tar.gz)",
      "tar -czf /tmp/<name>_$(date +%s).tar.gz <dir>",            2 },
    { 8, "Memory & CPU Snapshot",
      "free -h && top -bn1 | head -20",                           1 },
    { 9, "Custom Script",
      "You provide the full bash script content",                 2 },
};
#define N_TYPES (int)(sizeof(task_types) / sizeof(task_types[0]))

/* ════════════════════════════════════════════════════════
   LOG HELPERS
   ════════════════════════════════════════════════════════ */
static void clog_push(const char *fmt, ...) {
    char buf[158];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[157] = '\0';

    if (clog_count < CLOG_MAX) {
        strncpy(clog[clog_count], buf, 157);
        clog[clog_count][157] = '\0';
        clog_count++;
    } else {
        memmove(clog[0], clog[1], (CLOG_MAX - 1) * 160);
        strncpy(clog[CLOG_MAX - 1], buf, 157);
        clog[CLOG_MAX - 1][157] = '\0';
    }
}

/* ════════════════════════════════════════════════════════
   TUI DRAW HELPERS
   ════════════════════════════════════════════════════════ */
static void draw_log_panel(void) {
    if (!win_log) return;
    werase(win_log);
    box(win_log, 0, 0);
    wattron(win_log, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_log, 0, 2, " CLIENT LOG ");
    wattroff(win_log, COLOR_PAIR(3) | A_BOLD);

    int rows, cols;
    getmaxyx(win_log, rows, cols);
    int max_show = rows - 2;
    int start    = clog_count > max_show ? clog_count - max_show : 0;

    for (int i = start; i < clog_count; i++) {
        if (clog_count - i <= 2)
            wattron(win_log, A_BOLD);
        mvwprintw(win_log, i - start + 1, 2,
                  "%-*.*s", cols - 4, cols - 4, clog[i]);
        wattroff(win_log, A_BOLD);
    }
    wrefresh(win_log);
}

static void draw_header(void) {
    if (!win_header) return;
    werase(win_header);
    wattron(win_header, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win_header, 0, 2,
              " ☁  CLOUD TASK SCHEDULER — CLIENT   server:%s:%d ",
              SERVER_IP, PORT);
    wattroff(win_header, COLOR_PAIR(1) | A_BOLD);
    wrefresh(win_header);
}

static void draw_footer(const char *hint) {
    if (!win_footer) return;
    werase(win_footer);
    wattron(win_footer, COLOR_PAIR(2));
    mvwprintw(win_footer, 0, 1, " %s", hint);
    wattroff(win_footer, COLOR_PAIR(2));
    wrefresh(win_footer);
}

static void draw_menu(int selected) {
    if (!win_menu) return;
    werase(win_menu);
    box(win_menu, 0, 0);
    wattron(win_menu, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_menu, 0, 2, " SELECT TASK TYPE ");
    wattroff(win_menu, COLOR_PAIR(3) | A_BOLD);

    for (int i = 0; i < N_TYPES; i++) {
        if (i == selected)
            wattron(win_menu, COLOR_PAIR(6) | A_BOLD | A_REVERSE);
        else
            wattron(win_menu, COLOR_PAIR(7));

        mvwprintw(win_menu, i + 2, 3,
                  " [%d] %-24s  %s ",
                  task_types[i].id,
                  task_types[i].label,
                  task_types[i].description);

        if (i == selected)
            wattroff(win_menu, COLOR_PAIR(6) | A_BOLD | A_REVERSE);
        else
            wattroff(win_menu, COLOR_PAIR(7));
    }
    wrefresh(win_menu);
}

/* ════════════════════════════════════════════════════════
   FORM INPUT
   ════════════════════════════════════════════════════════ */
static int form_input(int row, const char *label, char *out, int maxlen) {
    if (!win_form || !out || maxlen <= 0) return 0;

    int win_rows, win_cols;
    getmaxyx(win_form, win_rows, win_cols);
    (void)win_rows;

    /*
     *  Column layout:
     *    col  2 : label starts
     *    col 32 : input field starts  (2 + 28 chars label + 2 for ": ")
     *    remaining cols - 2 right margin : field width
     */
    const int label_col = 2;
    const int field_col = 32;
    int       field_w   = win_cols - field_col - 2;
    if (field_w <= 0) field_w = 1;

    /* clamp maxlen so we never read more than fits visually */
    if (maxlen - 1 > field_w)
        maxlen = field_w + 1;

    /* draw label */
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, row, label_col, "%-28s: ", label);
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);

    /* draw blank underlined field */
    wattron(win_form, A_UNDERLINE);
    wmove(win_form, row, field_col);
    for (int i = 0; i < field_w; i++)
        waddch(win_form, ' ');
    wattroff(win_form, A_UNDERLINE);

    /* position cursor and refresh before reading */
    wmove(win_form, row, field_col);
    wrefresh(win_form);

    echo();
    curs_set(1);
    keypad(win_form, TRUE);

    wgetnstr(win_form, out, maxlen - 1);

    noecho();
    curs_set(0);
    keypad(win_form, TRUE);   /* re-assert after input */

    /* defensive null-terminate */
    out[maxlen - 1] = '\0';

    /* strip trailing newline */
    int len = (int)strlen(out);
    if (len > 0 && out[len - 1] == '\n')
        out[--len] = '\0';

    /* redraw field with entered value for visual confirmation */
    wattron(win_form, A_UNDERLINE);
    mvwprintw(win_form, row, field_col, "%-*.*s", field_w, field_w, out);
    wattroff(win_form, A_UNDERLINE);
    wrefresh(win_form);

    return len;
}

/* ════════════════════════════════════════════════════════
   TUI INIT / CLEANUP
   ════════════════════════════════════════════════════════ */
static void tui_init(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK,  COLOR_CYAN);    /* header    */
    init_pair(2, COLOR_BLACK,  COLOR_YELLOW);  /* footer    */
    init_pair(3, COLOR_CYAN,   -1);            /* titles    */
    init_pair(4, COLOR_GREEN,  -1);            /* ok        */
    init_pair(5, COLOR_RED,    -1);            /* error     */
    init_pair(6, COLOR_YELLOW, -1);            /* selected  */
    init_pair(7, COLOR_WHITE,  -1);            /* normal    */

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    /* terminal size guard */
    if (rows < 20 || cols < 60) {
        endwin();
        fprintf(stderr,
                "Terminal too small (%d rows x %d cols). "
                "Need at least 20 x 60.\n", rows, cols);
        exit(1);
    }

    /*
     *  Layout (top → bottom):
     *
     *   row 0                           win_header   1 row  (fixed)
     *   row 1                           win_menu     menu_h rows
     *   row 1 + menu_h                  win_form     form_h rows
     *   row 1 + menu_h + form_h         win_log      log_h  rows
     *   row rows-1                      win_footer   1 row  (fixed)
     *
     *  Constraint: 1 + menu_h + form_h + log_h + 1 == rows
     *  → log_h = rows - menu_h - form_h - 2
     *
     *  If log_h < 4 the terminal is too short for the fixed panels.
     *  We shrink form_h first, then menu_h, before giving up.
     */
    int menu_h = N_TYPES + 4;   /* box border (2) + header row (1) + spare (1) */
    int form_h = 12;

    int log_h = rows - menu_h - form_h - 2;
    if (log_h < 4) {
        form_h = rows - menu_h - 2 - 4;   /* give log 4 rows minimum */
        log_h  = 4;
        if (form_h < 4) {
            form_h = 4;
            menu_h = rows - form_h - 4 - 2;
            log_h  = 4;
            if (menu_h < 4) {
                endwin();
                fprintf(stderr, "Terminal too small to fit layout.\n");
                exit(1);
            }
        }
    }

    /* anchor win_footer to the very last row so there is never a gap */
    int log_start    = 1 + menu_h + form_h;
    int footer_start = rows - 1;
    /* recalculate log_h to fill exactly up to the footer */
    log_h = footer_start - log_start;
    if (log_h < 2) log_h = 2;

    win_header = newwin(1,       cols, 0,            0);
    win_menu   = newwin(menu_h,  cols, 1,            0);
    win_form   = newwin(form_h,  cols, 1 + menu_h,   0);
    win_log    = newwin(log_h,   cols, log_start,    0);
    win_footer = newwin(1,       cols, footer_start,  0);

    /* NULL-check: newwin() returns NULL silently on bad dimensions */
    if (!win_header || !win_menu || !win_form ||
        !win_log    || !win_footer) {
        endwin();
        fprintf(stderr,
                "newwin() failed — terminal too small or ncurses error.\n");
        exit(1);
    }

    /* enable arrow / function keys on every window */
    keypad(win_header, TRUE);
    keypad(win_menu,   TRUE);
    keypad(win_form,   TRUE);
    keypad(win_log,    TRUE);
    keypad(win_footer, TRUE);
}

static void tui_cleanup(void) {
    if (win_header) delwin(win_header);
    if (win_menu)   delwin(win_menu);
    if (win_form)   delwin(win_form);
    if (win_log)    delwin(win_log);
    if (win_footer) delwin(win_footer);
    endwin();
}

/* ════════════════════════════════════════════════════════
   SCRIPT GENERATORS
   Each fills `buf` with valid bash content.
   ════════════════════════════════════════════════════════ */

static void gen_compile_cpp(char *buf, int maxlen) {
    char file[128] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Compile & Run C++ ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "Source file (e.g. main.cpp)", file, 120);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Compile & Run: %s ==='\n"
        "g++ -std=c++17 '%s' -o /tmp/cjob_out 2>&1\n"
        "if [ $? -ne 0 ]; then echo 'COMPILE FAILED'; exit 1; fi\n"
        "echo '=== Running binary ==='\n"
        "/tmp/cjob_out\n"
        "echo \"=== Exit code: $? ===\"\n",
        file, file);
}

static void gen_run_python(char *buf, int maxlen) {
    char file[128] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Run Python Script ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "Python file (e.g. train.py)", file, 120);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Run Python: %s ==='\n"
        "python3 '%s'\n"
        "echo \"=== Exit: $? ===\"\n",
        file, file);
}

static void gen_backup(char *buf, int maxlen) {
    char src[128] = {0}, label[64] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Backup File ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "Source path",  src,   120);
    form_input(4, "Backup label", label, 60);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "DEST=\"/tmp/backup_%s_$(date +%%Y%%m%%d_%%H%%M%%S)\"\n"
        "echo \"=== Backing up %s -> $DEST ===\"\n"
        "cp '%s' \"$DEST\"\n"
        "echo \"=== Result: $? | Size: $(du -sh $DEST) ===\"\n",
        label, src, src);
}

static void gen_disk_report(char *buf, int maxlen) {
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "REPORT=\"/tmp/disk_report_$(date +%%s).txt\"\n"
        "echo '=== Disk Usage Report ===' | tee $REPORT\n"
        "df -h | tee -a $REPORT\n"
        "echo '' | tee -a $REPORT\n"
        "echo '=== Top 5 large dirs in /var ===' | tee -a $REPORT\n"
        "du -sh /var/* 2>/dev/null | sort -rh | head -5 | tee -a $REPORT\n"
        "echo \"Report saved: $REPORT\"\n");
}

static void gen_wc(char *buf, int maxlen) {
    char file[128] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Word / Line Count ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "File path", file, 120);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Word/Line/Char Count: %s ==='\n"
        "wc -lwc '%s'\n",
        file, file);
}

static void gen_find_delete(char *buf, int maxlen) {
    char dir[128] = {0}, ext[32] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Find & Delete ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "Search directory",     dir, 120);
    form_input(4, "Extension (e.g. tmp)", ext, 30);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Dry run: files to delete ==='\n"
        "find '%s' -name '*.%s' 2>/dev/null\n"
        "echo '=== Deleting... ==='\n"
        "find '%s' -name '*.%s' -delete 2>/dev/null\n"
        "echo '=== Done ==='\n",
        dir, ext, dir, ext);
}

static void gen_archive(char *buf, int maxlen) {
    char dir[128] = {0}, name[64] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Archive Directory ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(2, "Directory to archive", dir,  120);
    form_input(4, "Archive name",         name,  60);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "OUT=\"/tmp/%s_$(date +%%Y%%m%%d_%%H%%M%%S).tar.gz\"\n"
        "echo \"=== Archiving %s -> $OUT ===\"\n"
        "tar -czf \"$OUT\" '%s' 2>&1\n"
        "echo \"=== Result: $? | Archive size: $(du -sh $OUT) ===\"\n",
        name, dir, dir);
}

static void gen_mem_cpu(char *buf, int maxlen) {
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Memory & CPU Snapshot ==='\n"
        "echo '--- Memory ---'\n"
        "free -h\n"
        "echo '--- CPU (top 10 procs by CPU) ---'\n"
        "ps aux --sort=-%%cpu | head -11\n"
        "echo '--- Load Average ---'\n"
        "uptime\n");
}

static void gen_custom(char *buf, int maxlen) {
    char cmd[256] = {0};
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Custom Script ");
    mvwprintw(win_form, 1, 2,
              " Enter a single bash command (will be wrapped in a script) ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    form_input(3, "Bash command", cmd, 250);
    snprintf(buf, maxlen,
        "#!/bin/bash\n"
        "echo '=== Custom Job ==='\n"
        "%s\n"
        "echo \"=== Exit: $? ===\"\n",
        cmd);
}

/* ════════════════════════════════════════════════════════
   NETWORK
   ════════════════════════════════════════════════════════ */
static int send_job(const char *name, int priority,
                    const char *script) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        clog_push("ERROR: socket() failed");
        return -1;
    }

    struct sockaddr_in sv;
    memset(&sv, 0, sizeof(sv));
    sv.sin_family = AF_INET;
    sv.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &sv.sin_addr);

    if (connect(sock, (struct sockaddr *)&sv, sizeof(sv)) < 0) {
        clog_push("ERROR: connect() — is server running?");
        close(sock);
        return -1;
    }

    char prio_str[4]  = {0};
    char len_str[16]  = {0};
    snprintf(prio_str, sizeof(prio_str), "%d", priority);
    snprintf(len_str,  sizeof(len_str),  "%d", (int)strlen(script));

    /*
     *  Protocol: send name\0, prio\0, script_len\0, then raw script bytes.
     *  Small usleep between sends lets the server recv() each field
     *  as a separate chunk (avoids Nagle-algorithm merging on loopback).
     */
    send(sock, name,     strlen(name)     + 1, 0); usleep(30000);
    send(sock, prio_str, strlen(prio_str) + 1, 0); usleep(30000);
    send(sock, len_str,  strlen(len_str)  + 1, 0); usleep(30000);
    send(sock, script,   strlen(script),        0);

    char reply[256] = {0};
    recv(sock, reply, sizeof(reply) - 1, 0);
    reply[strcspn(reply, "\n")] = '\0';
    clog_push("Server: %s", reply);

    close(sock);
    return 0;
}

/* ════════════════════════════════════════════════════════
   PRIORITY SELECTOR
   ════════════════════════════════════════════════════════ */
static int pick_priority(int def) {
    const char *labels[] = { "", "LOW", "MEDIUM", "HIGH" };
    int sel = def;

    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2,
              " Select Priority (←→ or 1/2/3, Enter=confirm) ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);

    while (1) {
        for (int p = 1; p <= 3; p++) {
            if (p == sel)
                wattron(win_form, COLOR_PAIR(6) | A_BOLD | A_REVERSE);
            else
                wattron(win_form, COLOR_PAIR(7));

            mvwprintw(win_form, 2, 4 + (p - 1) * 14,
                      " [%d] %-6s ", p, labels[p]);

            if (p == sel)
                wattroff(win_form, COLOR_PAIR(6) | A_BOLD | A_REVERSE);
            else
                wattroff(win_form, COLOR_PAIR(7));
        }
        wrefresh(win_form);

        int ch = wgetch(win_form);
        if      (ch == KEY_RIGHT || ch == 'l') { if (sel < 3) sel++; }
        else if (ch == KEY_LEFT  || ch == 'h') { if (sel > 1) sel--; }
        else if (ch == '1') sel = 1;
        else if (ch == '2') sel = 2;
        else if (ch == '3') sel = 3;
        else if (ch == '\n' || ch == KEY_ENTER) break;
        else if (ch == 27) { sel = 0; break; }   /* ESC = cancel */
    }
    return sel;
}

/* ════════════════════════════════════════════════════════
   JOB NAME INPUT
   ════════════════════════════════════════════════════════ */
static void get_job_name(const char *suggested, char *out, int maxlen) {
    werase(win_form); box(win_form, 0, 0);
    wattron(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 0, 2, " Job Name ");
    wattroff(win_form, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_form, 1, 2, "(Press Enter to use default: %s)", suggested);
    form_input(3, "Job name", out, maxlen);
    if (strlen(out) == 0)
        strncpy(out, suggested, (size_t)(maxlen - 1));
    out[maxlen - 1] = '\0';
}

/* ════════════════════════════════════════════════════════
   MAIN
   ════════════════════════════════════════════════════════ */
int main(void) {
    tui_init();

    int selected = 0;

    draw_header();
    draw_menu(selected);
    draw_log_panel();
    draw_footer(" ↑↓ Navigate   Enter=Select   q=Quit ");
    clog_push("Client started. Server: %s:%d", SERVER_IP, PORT);
    draw_log_panel();

    while (1) {
        int ch = wgetch(win_menu);

        if      (ch == KEY_UP   && selected > 0)         selected--;
        else if (ch == KEY_DOWN && selected < N_TYPES-1) selected++;

        /* number shortcuts 1–9 */
        if (ch >= '1' && ch <= '9') {
            int idx = ch - '1';
            if (idx < N_TYPES) selected = idx;
        }

        if (ch == 'q' || ch == 'Q') break;

        draw_menu(selected);

        if (ch == '\n' || ch == KEY_ENTER) {
            const TaskType *tt = &task_types[selected];

            /* generate script */
            char script[SCRIPT_MAX] = {0};
            switch (tt->id) {
                case 1: gen_compile_cpp(script, SCRIPT_MAX); break;
                case 2: gen_run_python (script, SCRIPT_MAX); break;
                case 3: gen_backup     (script, SCRIPT_MAX); break;
                case 4: gen_disk_report(script, SCRIPT_MAX); break;
                case 5: gen_wc         (script, SCRIPT_MAX); break;
                case 6: gen_find_delete(script, SCRIPT_MAX); break;
                case 7: gen_archive    (script, SCRIPT_MAX); break;
                case 8: gen_mem_cpu    (script, SCRIPT_MAX); break;
                case 9: gen_custom     (script, SCRIPT_MAX); break;
                default: break;
            }

            if (strlen(script) == 0) {
                clog_push("Script generation cancelled.");
                draw_log_panel();
                draw_menu(selected);
                draw_footer(" ↑↓ Navigate   Enter=Select   q=Quit ");
                continue;
            }

            /* get job name */
            char job_name[NAME_LEN] = {0};
            get_job_name(tt->label, job_name, NAME_LEN);

            /* pick priority */
            int prio = pick_priority(tt->default_priority);
            if (prio == 0) {
                clog_push("Cancelled.");
                draw_log_panel();
                draw_menu(selected);
                draw_footer(" ↑↓ Navigate   Enter=Select   q=Quit ");
                continue;
            }

            /* send to server */
            const char *pl[] = { "", "LOW", "MEDIUM", "HIGH" };
            clog_push("Sending: \"%s\" [%s]", job_name, pl[prio]);
            draw_log_panel();

            if (send_job(job_name, prio, script) == 0)
                clog_push("Job submitted successfully.");
            else
                clog_push("FAILED to send job.");

            /* reset form area and redraw */
            werase(win_form);
            box(win_form, 0, 0);
            wrefresh(win_form);
            draw_log_panel();
            draw_menu(selected);
            draw_footer(" ↑↓ Navigate   Enter=Select   q=Quit ");
        }
    }

    tui_cleanup();
    printf("Client disconnected.\n");
    return 0;
}