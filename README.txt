================================================================
 Cloud Task Scheduler — README
 OS Lab Project | 2nd Year B.Tech ENTC
================================================================

WHAT THIS PROJECT DOES
-----------------------
A client-server task scheduler where:
  - The CLIENT queues real bash commands to a remote server
  - The SERVER executes them as child processes (fork+exec),
    captures their output, and logs results to files

FILES PRODUCED AT RUNTIME
  tasks.db          Binary flat-file database (fixed 512-byte records)
  notifications.txt Human-readable event log (task queued/done/failed)
  exec.log          Actual stdout+stderr output of every executed command


HOW TO BUILD & RUN
------------------
  make              # compiles both server and client

  Terminal 1:
    ./server        # starts server, listens on port 8080

  Terminal 2:
    ./client        # shows task menu


CLIENT MENU
-----------
  Enter a task number   → queue that specific task
  0                     → queue ALL 15 tasks at once
  -1                    → disconnect and exit


SERVER COMMANDS (type while server is running)
----------------------------------------------
  r   Run all pending tasks (executes in priority order: HIGH first)
  n   Run only the next highest-priority task
  l   List all tasks with ID, status, retry count, exit code
  e   Print exec.log (actual command output)
  v   Print notifications.txt (event log)
  q   Shutdown server


SYSTEM CALLS USED
-----------------

NETWORK
  socket()   Create TCP socket
  bind()     Attach socket to port 8080
  listen()   Mark socket as accepting connections
  accept()   Block until a client connects; returns new fd
  connect()  (client) Establish connection to server
  send()     Transmit data over socket
  recv()     Receive data from socket

FILE I/O
  open()     Open/create tasks.db, notifications.txt, exec.log
  read()     Read records from tasks.db; read log files
  write()    Write task records; write log entries
  lseek()    Jump to a specific record slot in tasks.db
  close()    Release file descriptors
  dup2()     Redirect child stdout/stderr → exec.log

PROCESS
  fork()     Spawn child worker for each task
  execl()    Replace child with /bin/bash -c "<command>"
  waitpid()  Parent waits for child; collects exit status
  getpid()   Child prints its own PID for tracing

MULTIPLEXING
  select()   Watch server socket + stdin simultaneously


EXAMPLE TASK WALKTHROUGH
-------------------------
1. Client sends task "Full System Report" (priority HIGH)
2. Server: add_task() saves record to tasks.db, appends to notifications.txt
3. Operator types 'r' on server
4. run_all_tasks() → picks highest priority first
5. execute_task():
     a. Marks task RUNNING in tasks.db
     b. open(exec.log)  — opens log file
     c. fork()          — creates child worker
     d. [child] dup2()  — redirects stdout/stderr → exec.log
     e. [child] execl("/bin/bash", "bash", "-c", command)
     f. [parent] waitpid() — waits for child
     g. Reads exit code; marks COMPLETED or retries
     h. Appends footer to exec.log
6. Operator types 'e' to view the captured output


RETRY LOGIC
-----------
  MAX_RETRY = 2 (defined in server.c)
  On non-zero exit code: retry up to 2 more times (1 second apart)
  After all retries exhausted: mark FAILED in tasks.db
  The "Intentional Failure" task in the catalog demonstrates this.


PRIORITY QUEUE
--------------
  run_next_task() scans all PENDING tasks and picks the highest
  Priority value (HIGH=3 > MEDIUM=2 > LOW=1).
  Ties are broken by first-in-first-served (lower array index wins).
================================================================
