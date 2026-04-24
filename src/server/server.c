/*
 * ============================================================
 *  Cloud Task Scheduler - SERVER (TUI Edition)
 *  Subject  : Operating Systems (2nd Year B.Tech ENTC)
 *  Language : C  |  Platform : Linux / POSIX
 *
 *  System Calls Used:
 *    File I/O  : open, write, read, lseek, close, dup2, mkdir
 *    Process   : fork, waitpid, getpid, execl
 *    Network   : socket, bind, listen, accept, recv, send
 *    Multiplex : select
 *
 *  TUI      : ncurses (4-panel layout)
 *  Compile  : gcc server.c -o server -lncurses
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/time.h>

/* File I/O & Process */
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* Network */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* TUI */
#include <ncurses.h>

/* ── Tunables ──────────────────────────────────────────── */
#define PORT        8080
#define DB_FILE     "tasks.db"
#define NOTIF_FILE  "notifications.txt"
#define JOBS_DIR    "jobs"
#define NAME_LEN    64
#define REC_SIZE    640
#define MAX_RETRY   2
#define MAX_TASKS   200
#define MAX_LOGS    200
#define SCRIPT_MAX  4096
#define POPUP_LINES 500
#define POPUP_COLS  512

/* ── Priority & Status ─────────────────────────────────── */
typedef enum { LOW = 1, MEDIUM = 2, HIGH = 3 }           Priority;
typedef enum { PENDING, RUNNING, COMPLETED, FAILED }      Status;

typedef struct {
    int      id;
    char     name[NAME_LEN];
    char     script_path[128];
    char     client_ip[20];
    Priority priority;
    Status   status;
    time_t   created_at;
    int      retry_count;
    int      exit_code;
} Task;

/* ── Globals ───────────────────────────────────────────── */
static Task tasks[MAX_TASKS];
static int  task_count = 0;
static int  next_id    = 1;
static int  db_fd      = -1;

/* ── TUI Windows — declared ONCE ──────────────────────── */
static WINDOW *win_header = NULL;
static WINDOW *win_queue  = NULL;
static WINDOW *win_log    = NULL;
static WINDOW *win_footer = NULL;

/* ── Log ring buffer ───────────────────────────────────── */
static char log_lines[MAX_LOGS][160];
static int  log_count = 0;

/*
 *  Pre-TUI log buffer.
 *
 *  db_open() / db_load() run BEFORE tui_init(), so ncurses
 *  windows don't exist yet.  We store those early messages here
 *  and flush them into the real TUI log after tui_init() returns.
 */
#define EARLY_LOG_MAX 32
static char early_log[EARLY_LOG_MAX][160];
static int  early_log_count = 0;
static int  tui_ready = 0;   /* set to 1 after tui_init() completes */

/* ════════════════════════════════════════════════════════
   LOG HELPERS
   ════════════════════════════════════════════════════════ */

/*
 *  tui_log — safe to call before tui_init().
 *  Before TUI is ready: stashes into early_log[].
 *  After TUI is ready : writes into log_lines[] ring buffer.
 */
static void tui_log(const char *fmt, ...) {
    char buf[158];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[157] = '\0';

    if (!tui_ready) {
        /* TUI not up yet — stash for later flush */
        if (early_log_count < EARLY_LOG_MAX) {
            strncpy(early_log[early_log_count], buf, 157);
            early_log[early_log_count][157] = '\0';
            early_log_count++;
        }
        return;
    }

    if (log_count < MAX_LOGS) {
        strncpy(log_lines[log_count], buf, 157);
        log_lines[log_count][157] = '\0';
        log_count++;
    } else {
        memmove(log_lines[0], log_lines[1], (MAX_LOGS - 1) * 160);
        strncpy(log_lines[MAX_LOGS - 1], buf, 157);
        log_lines[MAX_LOGS - 1][157] = '\0';
    }
}

/* Flush early_log[] into the live TUI ring buffer (called once after tui_init) */
static void flush_early_log(void) {
    for (int i = 0; i < early_log_count; i++)
        tui_log("%s", early_log[i]);
    early_log_count = 0;
}

/* ════════════════════════════════════════════════════════
   TUI DRAW HELPERS
   ════════════════════════════════════════════════════════ */

static void draw_header(void) {
    if (!win_header) return;
    werase(win_header);
    wattron(win_header, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win_header, 0, 2,
              " ☁  CLOUD TASK SCHEDULER — SERVER   port:%d   db:%s ",
              PORT, DB_FILE);
    wattroff(win_header, COLOR_PAIR(1) | A_BOLD);
    wrefresh(win_header);
}

static void draw_footer(void) {
    if (!win_footer) return;
    werase(win_footer);
    wattron(win_footer, COLOR_PAIR(2));
    mvwprintw(win_footer, 0, 1,
              " [r] Run All  [n] Next  [l] Refresh"
              "  [e] Exec-Log  [v] Notifs  [q] Quit");
    wattroff(win_footer, COLOR_PAIR(2));
    wrefresh(win_footer);
}

static void draw_queue(void) {
    if (!win_queue) return;
    werase(win_queue);
    box(win_queue, 0, 0);
    wattron(win_queue, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_queue, 0, 2, " TASK QUEUE (%d) ", task_count);
    wattroff(win_queue, COLOR_PAIR(3) | A_BOLD);

    int rows, cols;
    getmaxyx(win_queue, rows, cols);
    (void)cols;

    /* column headers */
    wattron(win_queue, A_UNDERLINE | A_BOLD);
    mvwprintw(win_queue, 1, 2, "%-4s %-20s %-7s %-10s %-6s %-5s",
              "ID", "Name", "Pri", "Status", "Retry", "Exit");
    wattroff(win_queue, A_UNDERLINE | A_BOLD);

    int max_show = rows - 3;
    int start    = task_count > max_show ? task_count - max_show : 0;

    for (int i = start; i < task_count && (i - start + 2) < rows - 1; i++) {
        Task *t = &tasks[i];

        const char *p = t->priority == HIGH   ? "HIGH  " :
                        t->priority == MEDIUM ? "MEDIUM" : "LOW   ";
        const char *s = t->status == PENDING   ? "PENDING  " :
                        t->status == RUNNING   ? "RUNNING  " :
                        t->status == COMPLETED ? "COMPLETED" : "FAILED   ";

        int pair = 7;
        if      (t->status == COMPLETED) pair = 4;
        else if (t->status == FAILED)    pair = 5;
        else if (t->status == RUNNING)   pair = 6;

        wattron(win_queue, COLOR_PAIR(pair));
        mvwprintw(win_queue, i - start + 2, 2,
                  "%-4d %-20s %-7s %-10s %-6d %-5d",
                  t->id, t->name, p, s, t->retry_count, t->exit_code);
        wattroff(win_queue, COLOR_PAIR(pair));
    }
    wrefresh(win_queue);
}

static void draw_log(void) {
    if (!win_log) return;
    werase(win_log);
    box(win_log, 0, 0);
    wattron(win_log, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win_log, 0, 2, " SERVER LOG ");
    wattroff(win_log, COLOR_PAIR(3) | A_BOLD);

    int rows, cols;
    getmaxyx(win_log, rows, cols);
    int max_show = rows - 2;
    int start    = log_count > max_show ? log_count - max_show : 0;

    for (int i = start; i < log_count; i++) {
        if (log_count - i <= 3)
            wattron(win_log, A_BOLD);
        mvwprintw(win_log, i - start + 1, 2,
                  "%-*.*s", cols - 4, cols - 4, log_lines[i]);
        wattroff(win_log, A_BOLD);
    }
    wrefresh(win_log);
}

static void redraw_all(void) {
    draw_header();
    draw_queue();
    draw_log();
    draw_footer();
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
    init_pair(4, COLOR_GREEN,  -1);            /* COMPLETED */
    init_pair(5, COLOR_RED,    -1);            /* FAILED    */
    init_pair(6, COLOR_YELLOW, -1);            /* RUNNING   */
    init_pair(7, COLOR_WHITE,  -1);            /* PENDING   */

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (rows < 10 || cols < 40) {
        endwin();
        fprintf(stderr, "Terminal too small (%dx%d): need at least 10x40.\n",
                rows, cols);
        exit(1);
    }

    /*
     *  Layout:
     *   row 0              → win_header  (1 row, fixed)
     *   row 1              → win_queue   (queue_h rows)
     *   row 1 + queue_h    → win_log     (log_h rows)
     *   row rows-1         → win_footer  (1 row, fixed)
     *
     *  Constraint: 1 + queue_h + log_h + 1 == rows
     *
     *  We calculate log_h by anchoring to footer so there is
     *  never a gap between win_log and win_footer.
     */
    int queue_h      = rows / 2;
    int log_start    = 1 + queue_h;
    int footer_start = rows - 1;
    int log_h        = footer_start - log_start;   /* fills exactly to footer */

    if (log_h < 2) {
        /* terminal is extremely short — give log minimum 2 rows */
        queue_h   = rows - 4;   /* 1 header + 2 log + 1 footer */
        log_start = 1 + queue_h;
        log_h     = 2;
        if (queue_h < 2) {
            endwin();
            fprintf(stderr, "Terminal too small to fit layout.\n");
            exit(1);
        }
    }

    win_header = newwin(1,       cols, 0,            0);
    win_queue  = newwin(queue_h, cols, 1,            0);
    win_log    = newwin(log_h,   cols, log_start,    0);
    win_footer = newwin(1,       cols, footer_start,  0);

    /*
     *  NULL-check BEFORE calling any ncurses function on the windows.
     *  keypad(NULL, ...) is undefined behaviour — it will crash.
     */
    if (!win_header || !win_queue || !win_log || !win_footer) {
        endwin();
        fprintf(stderr, "newwin() failed — terminal too small or ncurses error.\n");
        exit(1);
    }

    keypad(win_header, TRUE);
    keypad(win_queue,  TRUE);
    keypad(win_log,    TRUE);
    keypad(win_footer, TRUE);

    /* TUI is now fully initialised */
    tui_ready = 1;

    /* Replay any log messages that arrived before tui_init() */
    flush_early_log();

    redraw_all();
}

static void tui_cleanup(void) {
    if (win_header) { delwin(win_header); win_header = NULL; }
    if (win_queue)  { delwin(win_queue);  win_queue  = NULL; }
    if (win_log)    { delwin(win_log);    win_log    = NULL; }
    if (win_footer) { delwin(win_footer); win_footer = NULL; }
    endwin();
}

/* ════════════════════════════════════════════════════════
   POPUP FILE VIEWER
   ════════════════════════════════════════════════════════ */
static void tui_popup_file(const char *title, const char *filepath) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int pw = cols - 6;
    int ph = rows - 4;
    if (pw < 10) pw = 10;
    if (ph < 4)  ph = 4;

    WINDOW *pop = newwin(ph, pw, 2, 3);
    if (!pop) return;

    /*
     *  Fixed-size line buffer — avoids VLA on the stack.
     *  Each line is truncated to POPUP_COLS-1 characters.
     */
    static char lines[POPUP_LINES][POPUP_COLS];
    int lc = 0;

    FILE *f = fopen(filepath, "r");
    if (f) {
        char buf[POPUP_COLS];
        while (fgets(buf, sizeof(buf), f) && lc < POPUP_LINES) {
            buf[strcspn(buf, "\n")] = '\0';
            strncpy(lines[lc], buf, POPUP_COLS - 1);
            lines[lc][POPUP_COLS - 1] = '\0';
            lc++;
        }
        fclose(f);
    } else {
        strncpy(lines[0], "(file not found)", POPUP_COLS - 1);
        lc = 1;
    }

    int offset  = 0;
    int visible = ph - 2;
    keypad(pop, TRUE);

    while (1) {
        werase(pop);
        box(pop, 0, 0);
        wattron(pop, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(pop, 0, 2, " %s (↑↓ scroll, q=close) ", title);
        wattroff(pop, COLOR_PAIR(3) | A_BOLD);

        for (int i = 0; i < visible && (offset + i) < lc; i++)
            mvwprintw(pop, i + 1, 2, "%-*.*s",
                      pw - 4, pw - 4, lines[offset + i]);

        wrefresh(pop);

        int ch = wgetch(pop);
        if (ch == 'q' || ch == 'Q' || ch == 27)              break;
        if (ch == KEY_DOWN && offset + visible < lc) offset++;
        if (ch == KEY_UP   && offset > 0)            offset--;
    }

    delwin(pop);
    redraw_all();
}

/* ════════════════════════════════════════════════════════
   UTILITY
   ════════════════════════════════════════════════════════ */
static void get_time(char *buf, int size) {
    time_t t = time(NULL);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", localtime(&t));
}

static void notify(const char *msg) {
    char ts[32];
    get_time(ts, sizeof(ts));

    int fd = open(NOTIF_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char line[420];
        int  len = snprintf(line, sizeof(line), "[%s] %s\n", ts, msg);
        write(fd, line, len);
        close(fd);
    }
    tui_log("[ALERT] %s", msg);
    draw_log();
}

/* ════════════════════════════════════════════════════════
   DATABASE
   ════════════════════════════════════════════════════════ */
static void db_save(Task *t, int index) {
    char buf[REC_SIZE];
    memset(buf, 0, REC_SIZE);
    memcpy(buf, t, sizeof(Task));
    lseek(db_fd, (off_t)index * REC_SIZE, SEEK_SET);
    write(db_fd, buf, REC_SIZE);
}

static void db_load(void) {
    char buf[REC_SIZE];
    task_count = 0;
    next_id    = 1;
    lseek(db_fd, 0, SEEK_SET);
    while (read(db_fd, buf, REC_SIZE) == REC_SIZE) {
        memcpy(&tasks[task_count], buf, sizeof(Task));
        if (tasks[task_count].id >= next_id)
            next_id = tasks[task_count].id + 1;
        task_count++;
        if (task_count >= MAX_TASKS) break;
    }
    /* tui_log is safe here — if TUI isn't up yet it stashes the message */
    tui_log("Loaded %d task(s) from %s", task_count, DB_FILE);
}

static void db_open(void) {
    db_fd = open(DB_FILE, O_RDWR | O_CREAT, 0644);
    if (db_fd < 0) {
        /* TUI not up yet — write straight to stderr */
        perror("db_open");
        exit(1);
    }
    db_load();
}

/* ════════════════════════════════════════════════════════
   TASK MANAGEMENT
   ════════════════════════════════════════════════════════ */
static void add_task(const char *name, const char *script_content,
                     const char *client_ip, Priority priority) {
    if (task_count >= MAX_TASKS) {
        tui_log("Task list full!");
        return;
    }

    Task t;
    memset(&t, 0, sizeof(Task));
    t.id         = next_id++;
    t.priority   = priority;
    t.status     = PENDING;
    t.created_at = time(NULL);
    strncpy(t.name,      name,      sizeof(t.name)      - 1);
    strncpy(t.client_ip, client_ip, sizeof(t.client_ip) - 1);
    snprintf(t.script_path, sizeof(t.script_path),
             "%s/job_%d.sh", JOBS_DIR, t.id);

    /* write script to disk */
    int sfd = open(t.script_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (sfd >= 0) {
        ssize_t total = 0, len = (ssize_t)strlen(script_content);
        while (total < len) {
            ssize_t w = write(sfd, script_content + total, len - total);
            if (w <= 0) break;
            total += w;
        }
        close(sfd);
    } else {
        tui_log("WARN: could not write script %s", t.script_path);
    }

    tasks[task_count] = t;
    db_save(&tasks[task_count], task_count);
    task_count++;

    char msg[300];
    snprintf(msg, sizeof(msg),
             "Job #%d \"%s\" queued [%s] from %s -> %s",
             t.id, name,
             priority == HIGH ? "HIGH" : priority == MEDIUM ? "MEDIUM" : "LOW",
             client_ip, t.script_path);
    notify(msg);
    draw_queue();
}

static void execute_task(Task *t, int index) {
    char ts[32], log_path[140], header_buf[512], footer_buf[256];

    snprintf(log_path, sizeof(log_path), "%s/job_%d.log", JOBS_DIR, t->id);

    while (t->retry_count <= MAX_RETRY) {
        t->status = RUNNING;
        db_save(t, index);
        draw_queue();

        tui_log("[Job #%d] \"%s\" RUNNING%s  script:%s",
                t->id, t->name,
                t->retry_count > 0 ? " (RETRY)" : "",
                t->script_path);
        draw_log();

        /* open per-job log */
        int exec_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (exec_fd < 0) {
            tui_log("ERROR: cannot open log %s: %s",
                    log_path, strerror(errno));
            draw_log();
            break;
        }

        get_time(ts, sizeof(ts));
        int hlen = snprintf(header_buf, sizeof(header_buf),
            "\n══════════════════════════════════════\n"
            " Job #%d  |  %s  |  %s\n"
            " Script : %s\n"
            " Client : %s  |  Try: %d/%d\n"
            "══════════════════════════════════════\n",
            t->id, t->name, ts,
            t->script_path,
            t->client_ip, t->retry_count + 1, MAX_RETRY + 1);
        write(exec_fd, header_buf, hlen);

        /* fork */
        pid_t pid = fork();
        if (pid < 0) {
            tui_log("ERROR: fork() failed: %s", strerror(errno));
            draw_log();
            close(exec_fd);
            break;
        }

        if (pid == 0) {
            /* child — redirect stdout/stderr to log file, exec script */
            dup2(exec_fd, STDOUT_FILENO);
            dup2(exec_fd, STDERR_FILENO);
            close(exec_fd);
            execl("/bin/bash", "bash", t->script_path, (char *)NULL);
            perror("execl");
            exit(127);
        }

        close(exec_fd);

        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        int exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
        t->exit_code  = exit_code;

        /* write footer to log */
        exec_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (exec_fd >= 0) {
            get_time(ts, sizeof(ts));
            int flen = snprintf(footer_buf, sizeof(footer_buf),
                "──────────────────────────────────────\n"
                " Exit: %d  |  Finished: %s\n"
                "══════════════════════════════════════\n",
                exit_code, ts);
            write(exec_fd, footer_buf, flen);
            close(exec_fd);
        }

        if (exit_code == 0) {
            t->status = COMPLETED;
            db_save(t, index);
            char msg[300];
            snprintf(msg, sizeof(msg),
                     "Job #%d \"%s\" COMPLETED (exit 0). Log: %s",
                     t->id, t->name, log_path);
            notify(msg);
            draw_queue();
            return;
        }

        t->retry_count++;
        db_save(t, index);

        if (t->retry_count > MAX_RETRY) {
            t->status = FAILED;
            db_save(t, index);
            char msg[200];
            snprintf(msg, sizeof(msg),
                     "Job #%d \"%s\" FAILED after %d tries (exit %d).",
                     t->id, t->name, MAX_RETRY + 1, exit_code);
            notify(msg);
            draw_queue();
            return;
        }

        char msg[200];
        snprintf(msg, sizeof(msg),
                 "Job #%d retrying (%d/%d)...",
                 t->id, t->retry_count, MAX_RETRY);
        notify(msg);
        sleep(1);
    }
}

static void run_next_task(void) {
    int best = -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].status != PENDING) continue;
        if (best == -1 || tasks[i].priority > tasks[best].priority)
            best = i;
    }
    if (best == -1) {
        tui_log("No pending tasks.");
        draw_log();
        return;
    }
    execute_task(&tasks[best], best);
}

static void run_all_tasks(void) {
    int pending = 0;
    for (int i = 0; i < task_count; i++)
        if (tasks[i].status == PENDING) pending++;

    if (!pending) {
        tui_log("No pending tasks.");
        draw_log();
        return;
    }

    tui_log("Running %d pending task(s)...", pending);
    draw_log();

    while (1) {
        int found = 0;
        for (int i = 0; i < task_count; i++)
            if (tasks[i].status == PENDING) { found = 1; break; }
        if (!found) break;
        run_next_task();
    }

    tui_log("All tasks processed.");
    draw_log();
}

/* ════════════════════════════════════════════════════════
   NETWORK
   ════════════════════════════════════════════════════════ */
static void handle_client(int client_fd, const char *client_ip) {
    char name[NAME_LEN]     = {0};
    char prio_buf[4]        = {0};
    char len_buf[16]        = {0};
    char script[SCRIPT_MAX] = {0};

    if (recv(client_fd, name,     sizeof(name)     - 1, 0) <= 0) return;
    if (recv(client_fd, prio_buf, sizeof(prio_buf) - 1, 0) <= 0) return;
    if (recv(client_fd, len_buf,  sizeof(len_buf)  - 1, 0) <= 0) return;

    int script_len = atoi(len_buf);
    if (script_len <= 0 || script_len >= SCRIPT_MAX) {
        send(client_fd, "ERR: bad script len\n", 20, 0);
        return;
    }

    /* receive script body in a loop (TCP may fragment) */
    int received = 0;
    while (received < script_len) {
        int n = recv(client_fd, script + received,
                     script_len - received, 0);
        if (n <= 0) break;
        received += n;
    }
    script[received] = '\0';

    int p = atoi(prio_buf);
    if (p < 1 || p > 3) p = 1;

    add_task(name, script, client_ip, (Priority)p);

    char reply[256];
    snprintf(reply, sizeof(reply),
             "Job \"%s\" accepted (ID #%d). Press [r] on server to run.\n",
             name, next_id - 1);
    send(client_fd, reply, strlen(reply), 0);
}

/* ════════════════════════════════════════════════════════
   MAIN
   ════════════════════════════════════════════════════════ */
int main(void) {
    /* create jobs directory before anything else */
    mkdir(JOBS_DIR, 0755);

    /*
     *  IMPORTANT ORDER:
     *    1. db_open()  — may call tui_log() which stashes into early_log[]
     *    2. tui_init() — starts ncurses, flushes early_log[], draws UI
     *    3. socket setup — tui_log() now writes directly to win_log
     */
    db_open();
    tui_init();   /* tui_ready = 1 set inside; early log flushed here */

    /* ── Server socket ──────────────────────────────── */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        tui_cleanup();
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        tui_cleanup();
        perror("bind");
        return 1;
    }
    listen(server_fd, 10);

    tui_log("Server listening on port %d", PORT);
    tui_log("DB: %s  |  Jobs dir: %s/", DB_FILE, JOBS_DIR);
    draw_log();

    /* ── Event loop ─────────────────────────────────── */
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd, &fds);

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100 ms */
        int ready = select(server_fd + 1, &fds, NULL, NULL, &tv);

        /* incoming client connection */
        if (ready > 0 && FD_ISSET(server_fd, &fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int client_fd = accept(server_fd,
                                   (struct sockaddr *)&cli_addr, &cli_len);
            if (client_fd >= 0) {
                char *ip = inet_ntoa(cli_addr.sin_addr);
                tui_log("[+] Client connected: %s", ip);
                draw_log();
                handle_client(client_fd, ip);
                close(client_fd);
            }
        }

        /* keyboard input — non-blocking poll */
        nodelay(stdscr, TRUE);
        int ch = getch();
        nodelay(stdscr, FALSE);

        if (ch != ERR) {
            switch (ch) {
                case 'r': case 'R': run_all_tasks(); break;
                case 'n': case 'N': run_next_task(); break;
                case 'l': case 'L': redraw_all();    break;

                case 'e': case 'E': {
                    /* show the most recently executed job log */
                    int last = -1;
                    for (int i = 0; i < task_count; i++)
                        if (tasks[i].status == COMPLETED ||
                            tasks[i].status == FAILED)
                            last = i;
                    if (last == -1) {
                        tui_log("No executed jobs yet.");
                        draw_log();
                    } else {
                        char path[140];
                        snprintf(path, sizeof(path),
                                 "%s/job_%d.log", JOBS_DIR, tasks[last].id);
                        tui_popup_file("Exec Log", path);
                    }
                    break;
                }

                case 'v': case 'V':
                    tui_popup_file("Notifications", NOTIF_FILE);
                    break;

                case 'q': case 'Q':
                    goto shutdown;

                case KEY_RESIZE:
                    redraw_all();
                    break;

                default:
                    break;
            }
        }
    }

shutdown:
    close(server_fd);
    close(db_fd);
    tui_cleanup();
    printf("Server shut down.\n");
    return 0;
}