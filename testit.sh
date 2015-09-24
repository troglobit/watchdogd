#!/bin/sh

VERBOSE=0
if [ x"$1" = x"-v" ]; then
    VERBOSE=1
fi

./watchdogd -f -p --test-mode &
PID=$!
sleep 2

./examples/ex1
result=$?

kill $PID

if [ $result -ne 0 ]; then
    if [ $VERBOSE -eq 1 ]; then
	echo "Test FAIL! :-("
    fi
    exit 1
fi

if [ $VERBOSE -eq 1 ]; then
    echo "Test OK! :-)"
fi

exit 0
