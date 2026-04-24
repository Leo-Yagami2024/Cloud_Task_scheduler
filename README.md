# ☁️ Cloud Task Scheduler (TUI Based)

## 📌 Overview

This project is a **Client-Server based Cloud Task Scheduler** implemented in **C using POSIX system calls** and **ncurses (TUI)**.

It allows clients to submit jobs (scripts), which are executed by the server based on **priority scheduling**.

---

## 🚀 Features

- 📡 Client-Server architecture (TCP sockets)
- 🧠 Priority-based job scheduling (LOW, MEDIUM, HIGH)
- 🔁 Automatic retry mechanism on failure
- 📜 Script-based execution using `fork + exec`
- 💾 Persistent storage using file-based DB
- 🖥️ Terminal UI using ncurses
- 📊 Live logs and execution tracking

---

## 🛠️ Technologies Used

- C (POSIX)
- System Calls:
  - `fork`, `exec`, `waitpid`
  - `socket`, `bind`, `listen`, `accept`
  - `open`, `read`, `write`
  - `select`
- ncurses (TUI)

---

## 📂 Project Structure
