Watchdog with loadavg monitoring
================================
[![Travis Status][]][Travis]

Table of Contents
-----------------

* [Introduction](#introduction)
* [Usage](#usage)
* [Features](#features)
* [Pmon API](#pmon-api)
* [Operation](#operation)
* [Debugging](#debugging)
* [Origin & References](#origin--references)
* [Contributing](#contributing)


Introduction
------------

A watchdog timer (WDT) is something most motherboards of laptops and
servers today are equipped with.  It is basically a small timer that is
connected to the reset circuitry so that it can reset the board when the
timer expires.

The Linux kernel provides a common userspace interface `/dev/watchdog`,
created automatically when the appropriate driver module is loaded.  If
your board does not have a WDT the kernel provides a "softdog" module
which could be good enough.

The idea is to have a watchdog daemon in userspace that runs in the
background of your system.  When there is no more CPU time for the
watchdog daemon to run it will fail to "kick" the WDT.  This will in
turn cause the WDT to reboot the system.

> If you have an embedded system -- you need a watchdog daemon.

Most embedded systems utilise this as a way to automatically recover
when they get stuck.

Without arguments *watchdogd* can be used for this, but it can also be
used to detect other system problems:

- Load average
- Memory leaks
- File descriptor leaks
- Process live locks


Usage
-----

    watchdogd [-fxLsVvh] [-a WARN,REBOOT] [-T SEC] [-t SEC] [[-d] /dev/watchdog]
    
    Options:
      -d, --device=<dev>       Device to use, default: /dev/watchdog
      -f, --foreground         Start in foreground (background is default)
      -x, --external-kick[=N]  Force external watchdog kick using SIGUSR1
                               A 'N x <interval>' delay for startup is given
      -l, --logfile=<file>     Log to <file> in background, otherwise silent
      -L, --syslog             Use syslog, even if in foreground
      -w, -T, --timeout=<sec>  Set the HW watchdog timeout to <sec> seconds
      -k, -t, --interval=<sec> Set watchdog kick interval to <sec> seconds
      -s, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM
      
      -a, --load-average=<val> Enable load average check <WARN,REBOOT>
      -m, --meminfo=<val>      Enable memory leak check, <WARN,REBOOT>
      -n, --filenr=<val>       Enable file descriptor leak check, <WARN,REBOOT>
      -p, --pmon[=PRIO]        Enable process monitor, run at elevated RT prio
                               Default RT prio when active: SCHED_RR @98

      -V, --verbose            Verbose, noisy output suitable for debugging
      -v, --version            Display version and exit
      -h, --help               Display this help message and exit
    
By default, watchdogd opens `/dev/watchdog`, attempts to set 20 sec WDT
timeout and then kicks, in the background, every 10 sec.

**Example**

    watchdogd -d /dev/watchdog2 -a 0.8,0.9 -w 120 -k 30

Most WDT drivers only support 120 sec as lowest timeout, but watchdogd
tries to set 20 sec timeout.  Example values above are recommendations

watchdogd runs at the default UNIX priority (nice) level.


Features
--------

watchdogd can be used stand-alone to kick a kernel `/dev/watchdog`, or
with an external supervisor.  The latter must use `SIGUSR1` to activate
external kicks.  Use `--external-kick[=NUM]` to force an external
supervisor daemon, where NUM is an optional delay which can be quite
useful at system startup.  E.g., with `NUM=3` watchdogd will delay the
handover three built-in kicks, providing the external supervisor enough
time to start.

An external supervisor often need to lookup the PID to be able to send
signals, watchdogd stores its PID in `/var/run/watchdogd.pid` like any
other daemon.

To force a kernel watchdog reboot, watchdogd supports `SIGPWR`.  What it
does is to set the WDT timer to the lowest possible value (1 sec), close
the connection to `/dev/watchdog`, and wait for WDT reboot.  It waits at
most 3x the WDT timeout before announcing HW WDT failure and forcing a
reboot.

watchdogd supports monitoring of several system resources, all of which
are disabled by default.  First, system load average monitoring can be
enabled with the `-a 0.8,0.9` command line argument.  Second, the memory
leak detector `-m 0.9,0.95`.  Third, the file descriptor leak detector
`-n 0.8,0.95`.  All of which are *very* useful on an embedded system!

The two values, separated by a comma, are the warning and reboot levels
in percent.  For the loadavg monitoring it is important to know that the
trigger levels are normalized.  This means watchdogd does not care how
many CPU cores your system has online.  If the kernel `/proc/loadavg`
file shows `3.9 3.0 2.5` on a four-core CPU, watchdogd will consider
this as a load of `0.98 0.75 0.63`, i.e. divided by four.  Only the one
(1) and five (5) minute average values are used.  For more information
on the UNIX load average, see this [StackOverflow question][loadavg].

The RAM usage monitor only triggers on systems without swap.  This is
detected by reading the file `/proc/meminfo`, looking for the
`SwapTotal:` value.  For more details on the underlying mechanisms of
file descriptor usage, see [this article][filenr].  For more info on the
details of memory usage, see [this article][meminfo].

Also, watchdogd v2.0 comes with a process monitor, pmon.  It must be
enabled and a monitored client must connect using the API for pmon to
start.  As soon pmon starts it raises the real-time priority of
watchdogd to 98 to be able to ensure proper monitoring of its clients.


Pmon API
--------

To use pmon a client must have its source code instrumented with at
least a "subscribe" and a "kick" call.  Commonly this is achieved by
adding the `wdog_pmon_kick()` call to the main event loop.

All API calls, except `wdog_pmon_ping()`, return POSIX OK(0) or negative
value with `errno` set on error.  The `wdog_pmon_subscribe()` call
returns a positive integer (including zero) for the watchdog `id`.

```C

    /*
     * Enable or disable watchdogd at runtime,
     * i.e., if upgrading flash or similar.
	 */
    int wdog_enable           (int enable);
	int wdog_status           (int *enabled);
	
    /*
	 * Check if watchdogd API is actively responding,
	 * returns %TRUE(1) or %FALSE(0)
	 */
	int wdog_pmon_ping        (void);

    /*
     * Register with pmon, timeout in msec.  The return value is the `id`
     * to be used with the `ack` in subsequent kick/unsubscribe.
     */
	int wdog_pmon_subscribe   (char *label, int timeout, int *ack);
	int wdog_pmon_unsubscribe (int id, int ack);
	int wdog_pmon_kick        (int id, int *ack);

```

It is recommended to use an event loop library like libev, [libuev][],
or similar.  For such libraries one can simply add a timer callback for
the kick to run periodically to monitor proper operation of the client.

### Example

For other applications, identify your main loop, its max period time and
instrument it like this:

```C

    int ack, wid = wdog_pmon_subscribe(NULL, 10000, &ack);

    while (1) {
            ...
            wdog_pmon_kick(wid, &ack);
            ...
    }

```

This simple example subscribes to the watchdog with a 10 sec timeout.
The received `wid` is used in the call to `wdog_pmon_kick()`, along with
the received `ack` value.  Which is changed every time the application
calls `wdog_pmon_kick()`.  The application should of course check the
return value of `wdog_pmon_subscribe()` for errors, that code is left
out of the example to make it easier to read.


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

This is project is a heavily refactored and improved version of the
original watchdogd by Michele d'Amico, adapted to [uClinux-dist][] by
Mike Frysinger.  It is maintained by [Joachim Nilsson][] collaboratively
at [GitHub][].

The [original code][] in uClinux-dist has no license and is available in
the public domain, whereas this version is distributed under the ISC
license.  See the file [LICENSE][] for more on this.


Contributing
------------

If you find bugs or want to contribute fixes or features, check out the
code from GitHub, including the submodules:

	git clone https://github.com/troglobit/watchdogd
	cd watchdogd
	make submodules

When you pull from upstream, remember to also update the submodules
using `git submodule update`, see the file [CONTRIBUTING.md][contrib]
for details.


[uClinux-dist]:    http://www.uclinux.org/pub/uClinux/dist/
[loadavg]:         http://stackoverflow.com/questions/11987495/linux-proc-loadavg
[filenr]:          http://www.cyberciti.biz/tips/linux-procfs-file-descriptors.html
[meminfo]:         http://www.cyberciti.biz/faq/linux-check-memory-usage/
[original code]:   http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[libuev]:          https://github.com/troglobit/libuev/
[Travis]:          https://travis-ci.org/troglobit/watchdogd
[Travis Status]:   https://travis-ci.org/troglobit/watchdogd.png?branch=master
[GitHub]:          http://github.com/troglobit/watchdogd
[LICENSE]:         https://github.com/troglobit/watchdogd/blob/master/LICENSE
[contrib]:         https://github.com/troglobit/watchdogd/blob/master/CONTRIBUTING.md
[Joachim Nilsson]: http://troglobit.com
[the FTP]:         http://ftp.troglobit.com/watchdogd/
[releases page]:   https://github.com/troglobit/watchdogd/releases

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
