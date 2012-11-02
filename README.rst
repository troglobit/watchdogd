==============================================================================
                    watchdogd - A Small Watchdog Daemon
==============================================================================

This is the watchdogd from uClinux-dist, originally by Michele d'Amico
and later adapted to uClinux-dist by Mike Frysinger.

	http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html

The original code has no license and is available in the public domain
from the following location.  See the file LICENSE for more on this.

	http://www.uclinux.org/pub/uClinux/dist/

Features
--------

The watchdogd can be used stand-alone to kick a kernel watchdog at
/dev/watchdog, or with an external supervisor.  The latter must use
SIGUSR1 to activate external kicks.  To force an external supervisor
daemon, use ``--external-kick[=NUM]``, where NUM is an optional delay
which can be quite useful at system startup.  E.g., with `NUM=3`
watchdogd will delay the handover three built-in kicks, providing the
external supervisor enough time to start.

An external supervisor often need to lookup the PID to be able to send
signals, watchdogd stores its PID in ``/var/run/watchdogd.pid`` like any
other daemon.

Operation
---------

Without any arguments watchdogd forks off a daemon in the background,
opens the /dev/watchdog device, attempts to set the default watchdog
timeout to 20 seconds and then goes into an endless loop where it kicks
the watchdog every 10 seconds.

If the device driver does not support setting the watchdog timeout
watchdogd attempts to query the actual (possibly hard coded) watchdog
timeout and then uses half that time as the kick interval.

When the daemon backgrounds itself syslog is implicitly used for all
informational and debug messages.  If a user requests to run the daemon
in the foreground the ``--syslog``, or ``-L``, argument can be used to
redirect STDERR/STDOUT to the syslog.


Debugging
---------

The code has both INFO() and DEBUG() statements sprinkled almost
everywhere.  Enable ``--verbose`` and use ``--syslog`` a logfile
or ``--foreground`` to get debug output to the terminal.


Contact
-------

This project is maintained at http://github.com/troglobit/watchdogd —
please file a bug report, clone it, or send pull requests for bug fixes
and proposed extensions, or become a co-maintainer by contacting the
main author.

Regards
 /Joachim Nilsson <troglobit@gmail.com>

