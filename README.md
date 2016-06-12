Advanced System & Process Supervisor
====================================
[![Travis Status][]][Travis] [![Coverity Status][]][Coverity Scan]

Table of Contents
-----------------

* [Introduction](#introduction)
* [Usage](#usage)
* [Features](#features)
* [Pmon API](#pmon-api)
* [Operation](#operation)
* [Debugging](#debugging)
* [Build & Install](#build--install)
* [Origin & References](#origin--references)
* [Contributing](#contributing)


Introduction
------------

watchdogd is an advanced system and process supervisor daemon.  It can
monitor critical system resources, supervise the heartbeat of processes
and record deadline transgressions before safely rebooting your system.

When the system boots back up watchdogd queries the system to determine
the cause of the (re)boot and records it in a logfile for later analysis
by an operator or network management system (NMS).

### What is watchdog timer?

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

### What can watchdogd do?

Without arguments *watchdogd* simply runs in the background, "kicking"
the WDT (via the driver).  However, with few command line options it can
also detect system problems such as:

- Load average
- Memory leaks
- File descriptor leaks
- Process live locks


Usage
-----

    watchdogd [-hnsVvx] [-a WARN,REBOOT] [-T SEC] [-t SEC] [/dev/watchdog]
    
    Options:
      -n, --foreground         Start in foreground (background is default)
      -s, --syslog             Use syslog, even if running in foreground
      -l, --loglevel=LVL       Log level: none, err, info, notice*, debug
      
      -T, --timeout=SEC        Set the HW watchdog timeout to <sec> seconds
      -t, --interval=SEC       Set watchdog kick interval to <sec> seconds
      -x, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM
      
      -a, --load-average=W,R   Enable load average check <WARN,REBOOT>
      -m, --meminfo=W,R        Enable memory leak check, <WARN,REBOOT>
      -f, --filenr=W,R         Enable file descriptor leak check, <WARN,REBOOT>
      -p, --pmon[=PRIO]        Enable process monitor, run at elevated RT prio
                               Default RT prio when active: SCHED_RR @98
      
      -v, --version            Display version and exit
      -h, --help               Display this help message and exit
    
By default, with any arguments given on the command line, `watchdogd`
opens `/dev/watchdog`, forks to the background and then tries to to set
a 20 sec WDT timeout.  It then kicks every 10 sec.  If it fails setting
the 20 sec WDT timeout it attempts to read back the lowest possible WDT
timeout and kicks every half that interval.

**Example**

    watchdogd -a 0.8,0.9 -T 120 -t 30 /dev/watchdog2

Most WDT drivers only support 120 sec as lowest timeout, but watchdogd
tries to set 20 sec timeout.  Example values above are recommendations

watchdogd runs at the default UNIX priority (nice) level.


Features
--------

To force a kernel watchdog reboot, watchdogd supports `SIGPWR`.  What it
does is to set the WDT timer to the lowest possible value (1 sec), close
the connection to `/dev/watchdog`, and wait for WDT reboot.  It waits at
most 3x the WDT timeout before announcing HW WDT failure and forcing a
reboot.

watchdogd supports monitoring of several system resources, all of which
are disabled by default.  First, system load average monitoring can be
enabled with the `-a 0.8,0.9` command line argument.  Second, the memory
leak detector `-m 0.9,0.95`.  Third, the file descriptor leak detector
`-f 0.8,0.95`.  All of which are *very* useful on an embedded system!

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
     * Enable or disable watchdogd at runtime.
	 */
    int wdog_enable           (int enable);
	int wdog_status           (int *enabled);
	
    /*
	 * Check if watchdogd API is actively responding,
	 * returns %TRUE(1) or %FALSE(0)
	 */
	int wdog_pmon_ping        (void);

    /*
     * Register with pmon, timeout in msec.  Return value is the `id`
     * to be used with the `ack` in subsequent kick()/unsubscribe()
     */
	int wdog_pmon_subscribe   (char *label, int timeout, int *ack);
	int wdog_pmon_unsubscribe (int id, int ack);
	int wdog_pmon_kick        (int id, int *ack);

```

It is highly recommended to use an event loop like libev, [libuev][], or
similar.  For such libraries one can simply add a timer callback for the
kick to run periodically to monitor proper operation of the client.


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

See also the [example/ex1.c][ex1] in the source distribution.  This is
used by the automatic tests.


Operation
---------

By default, watchdogd forks off a daemon in the background, opens the
`/dev/watchdog` device, attempts to set the default WDT timeout to 20
seconds and then goes into an endless loop where it kicks the watchdog
every 10 seconds.

If the device driver does not support setting the WDT timeout watchdogd
attempts to query the actual (possibly hard coded) watchdog timeout and
then uses half that time as the kick interval.

When watchdogd backgrounds itself syslog is implicitly used for all
informational and debug messages.  If a user requests to run the daemon
in the foreground watchdogd will use STDERR/STDOUT, unless the user
gives the `--syslog`, or `-L`, argument to force use of syslog.


Debugging
---------

The code has both `INFO()` and `DEBUG()` statements sprinkled almost
everywhere.  Use the `--loglevel=debug` command line option to enable
full debug output to stderr or the syslog, depending on how you start
`watchdogd`.


Build & Install
---------------

watchdogd is tailored for Linux systems and should build against any
(old) C libray.  However, watchdogd v2.1 and later require two external
libraries that were previously a built-in, [libite][] and [libuEv][].
Neither of them should present any surprises, both use de facto standard
configure script.

One thing of importance though, the current build system for watchdogd
requires that the build prefix for both depending libraries and also
for watchdogd is the same.  I.e.,

For libite and libuev:

    ./configure --prefix=/opt/secret

Would require watchdogd to be built with:

    make distclean
    prefix=/opt/secret make
    prefix=/opt/secret make test

To build the source from GIT, see below.


Origin & References
-------------------

watchdogd is a heavily refactored and improved version of the original
watchdogd by Michele d'Amico, which was adapted to [uClinux-dist][] by
Mike Frysinger.  It is maintained by [Joachim Nilsson][] collaboratively
at [GitHub][].

The [original code][] in uClinux-dist has no license and is available in
the public domain, whereas this version is distributed under the ISC
license.  See the file [LICENSE][] for more details on this.


Contributing
------------

If you find bugs or want to contribute fixes or features, check out the
code from GitHub:

	git clone https://github.com/troglobit/watchdogd
	cd watchdogd

For more details, see [CONTRIBUTING][contrib].


[uClinux-dist]:    http://www.uclinux.org/pub/uClinux/dist/
[loadavg]:         http://stackoverflow.com/questions/11987495/linux-proc-loadavg
[filenr]:          http://www.cyberciti.biz/tips/linux-procfs-file-descriptors.html
[meminfo]:         http://www.cyberciti.biz/faq/linux-check-memory-usage/
[original code]:   http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[libite]:          https://github.com/troglobit/libite/
[libuEv]:          https://github.com/troglobit/libuev/
[Travis]:          https://travis-ci.org/troglobit/watchdogd
[Travis Status]:   https://travis-ci.org/troglobit/watchdogd.png?branch=master
[Coverity Scan]:   https://scan.coverity.com/projects/6458
[Coverity Status]: https://scan.coverity.com/projects/6458/badge.svg
[GitHub]:          http://github.com/troglobit/watchdogd
[ex1]:             https://github.com/troglobit/watchdogd/blob/master/examples/ex1.c
[LICENSE]:         https://github.com/troglobit/watchdogd/blob/master/LICENSE
[contrib]:         https://github.com/troglobit/watchdogd/blob/master/CONTRIBUTING.md
[Joachim Nilsson]: http://troglobit.com

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
