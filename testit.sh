#!/bin/sh

ARGD=""
ARGE=""
if [ x"$1" = x"-v" -o x"$1" = x"-V" ]; then
    ARGD="-l debug"
    ARGE="-V"
fi

echo "Starting test ..."

timeout 5s ./examples/ex1 $ARGE
if [ $? -eq 0 ]; then
    echo "Test FAIL! (1)"
    exit 1
fi
sleep 1

echo "Starting watchdogd ..."
./watchdogd $ARGD -n -p --test-mode &
WDOG=$!
sleep 5

# This test should take ~40 seconds to run
echo "Starting API test ..."
timeout 45s ./examples/ex1 $ARGE
result=$?

# Stop watchdogd
kill $WDOG

if [ $result -ne 0 ]; then
    echo "Test FAIL! (2)"
    exit 1
fi

echo "Test OK!"
exit 0
