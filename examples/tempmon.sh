#!/bin/sh

TEMP=/sys/class/thermal/thermal_zone0/temp

check()
{
    awk '{temp=$1; temp=temp/1000; rc=sprintf("%.1f", temp); exit rc < 55.0 }' < "$1"
}

if [ ! -f "$TEMP" ]; then
    logger -sk -t tempmon -I $PPID -p user.warn "No such sensor $TEMP"
    exit 1
fi

if check "$TEMP"; then
    echo "Too hot!"
    exit 10
fi
