#!/bin/sh

ARG=""
if [ x"$1" = x"-v" -o x"$1" = x"-V" ]; then
    ARG="-V"
fi

./watchdogd $ARG -f -p --test-mode &
WDOG=$!
sleep 2

# This test should take ~40 seconds to run
echo "Starting API test ..."
timeout 45s ./examples/ex1 $ARG
result=$?

# Stop watchdogd
kill $WDOG

if [ $result -ne 0 ]; then
    echo "Test FAIL!"
    exit 1
fi

echo "Test OK!"
exit 0
