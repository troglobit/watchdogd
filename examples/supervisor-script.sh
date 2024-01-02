#!/bin/sh

role=$1
code=$2
pid=$3
label=$4

case $code in
    3)
	logger -sp user.err -I $$ "process $pid failed a kick, cause $code, restarting service."
	;;
    5)
	logger -sp user.err -I $$ "process $pid failed to meet its deadline, cause $code"
	logger -sp user.err -I $$ "this cause is unrecoverable, exit 1!"
	exit 1
	;;
    *)
	;;
esac
    
logger -sp user.err -I $$ "system recovered, exit 0"
exit 0
