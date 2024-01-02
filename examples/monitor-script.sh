#!/bin/sh

PID=$(pidof sysrepo-plugind)
if [ -z "$PID" ]; then
#    logger -sk -t monitor -I $PPID -p user.error "sysrepo-plugind is not running"
    exit 0
fi
MEM=$(awk '/VmRSS/{print $2}' /proc/$PID/status)

logger -sk -t monitor -I $PPID -p user.notice "sysrepo-plugind memory usage: $MEM kB"
exit 0
