#!/bin/bash
echo '=== Memory & CPU Snapshot ==='
echo '--- Memory ---'
free -h
echo '--- CPU (top 10 procs by CPU) ---'
ps aux --sort=-%cpu | head -11
echo '--- Load Average ---'
uptime
