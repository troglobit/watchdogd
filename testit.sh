#!/bin/sh

ARG=""
if [ x"$1" = x"-v" -o x"$1" = x"-V" ]; then
    ARG="-V"
fi

timeout 5s ./examples/ex1 $ARG
if [ $? -eq 0 ]; then
    echo "Test FAIL! (1)"
    exit 1
fi
sleep 1

./watchdogd $ARG -f -p --test-mode &
WDOG=$!
sleep 5

# This test should take ~40 seconds to run
echo "Starting API test ..."
timeout 45s ./examples/ex1 $ARG
result=$?

# Stop watchdogd
kill $WDOG

if [ $result -ne 0 ]; then
    echo "Test FAIL! (2)"
    exit 1
fi

echo "Test OK!"
exit 0
