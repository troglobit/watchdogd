watchdogd | A small watchdog daemon
===================================

Table of Contents
-----------------

* [Introduction](#introduction)
* [Usage](#usage)
* [Features](#features)
* [Operation](#operation)
* [Debugging](#debugging)
* [Origin & References](#origin--references)


Introduction
------------

This is a slightly refactored and improved version of the original
watchdogd from [uClinux-dist].  It was written by Michele d'Amico and
later adapted to uClinux-dist by Mike Frysinger.


Usage
-----

    watchdogd [-fVL] [-w <sec>] [-k <sec>] [-s] [-x [NUM]]
	
    --foreground, -f         Start in foreground, background is default
    --external-kick, -x NUM  Force external watchdog kick using SIGUSR1
                             A 'NUM x INTERVAL' delay for startup is given
    --logfile, -l FILE       Log to FILE when backgrounding, otherwise silent
    --syslog, -L             Use syslog, even if in foreground
    --timeout, -w NUM        Set the HW watchdog timeout to NUM seconds
    --interval, -k NUM       Set watchdog kick interval to NUM seconds
    --safe-exit, -s          Disable watchdog on exit from SIGINT/SIGTERM
    --verbose, -V            Verbose operation, noisy output suitable for debugging
    --version, -v            Display version and exit
    --help, -h               Display this help message and exit


Features
--------

The watchdogd can be used stand-alone to kick a kernel watchdog at
`/dev/watchdog`, or with an external supervisor.  The latter must use
`SIGUSR1` to activate external kicks.  To force an external supervisor
daemon, use `--external-kick[=NUM]`, where NUM is an optional delay
which can be quite useful at system startup.  E.g., with `NUM=3`
watchdogd will delay the handover three built-in kicks, providing the
external supervisor enough time to start.

An external supervisor often need to lookup the PID to be able to send
signals, watchdogd stores its PID in `/var/run/watchdogd.pid` like any
other daemon.

To force a kernel watchdog reboot, watchdogd supports `SIGPWR`.  What
it does is to set the WDT timer to the lowest possible value (1 sec),
close the connection to `/dev/watchdog`, and wait for WDT reboot.


Operation
---------

Without any arguments watchdogd forks off a daemon in the background,
opens the `/dev/watchdog` device, attempts to set the default watchdog
timeout to 20 seconds and then goes into an endless loop where it kicks
the watchdog every 10 seconds.

If the device driver does not support setting the watchdog timeout
watchdogd attempts to query the actual (possibly hard coded) watchdog
timeout and then uses half that time as the kick interval.

When the daemon backgrounds itself syslog is implicitly used for all
informational and debug messages.  If a user requests to run the daemon
in the foreground the `--syslog`, or `-L`, argument can be used to
redirect STDERR/STDOUT to the syslog.


Debugging
---------

The code has both `INFO()` and `DEBUG()` statements sprinkled almost
everywhere.  Enable `--verbose` and use `--syslog` a logfile or
`--foreground` to get debug output to the terminal.


Origin & References
-------------------

The [original code] in uClinux-dist has no license and is available in
the public domain, whereas this version is distributed under the ISC
license.  See the file [LICENSE] for more on this.

This project is maintained by [Joachim Nilsson] collaboratively at
[GitHub].  Please file a bug reports, clone it, or send pull requests
for bug fixes and proposed extensions, or become a co-maintainer by
contacting the main author.

[uClinux-dist]:    http://www.uclinux.org/pub/uClinux/dist/
[original code]:   http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[GitHub]:          http://github.com/troglobit/watchdogd
[LICENSE]:         https://github.com/troglobit/watchdogd/blob/master/LICENSE
[Joachim Nilsson]: http://troglobit.com
