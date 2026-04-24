#!/bin/bash
echo '=== Compile & Run: #include <stdio.h> ==='
g++ -std=c++17 '#include <stdio.h>' -o /tmp/cjob_out 2>&1
if [ $? -ne 0 ]; then echo 'COMPILE FAILED'; exit 1; fi
echo '=== Running binary ==='
/tmp/cjob_out
echo "=== Exit code: $? ==="
