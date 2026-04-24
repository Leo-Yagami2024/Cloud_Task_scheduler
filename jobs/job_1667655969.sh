#!/bin/bash
echo '=== Compile & Run: 1 ==='
g++ -std=c++17 '1' -o /tmp/cjob_out 2>&1
if [ $? -ne 0 ]; then echo 'COMPILE FAILED'; exit 1; fi
echo '=== Running binary ==='
/tmp/cjob_out
echo "=== Exit code: $? ==="
