System & Process Supervisor for Linux
=====================================
[![License Badge][]][License] [![Travis Status][]][Travis] [![Coverity Status][]][Coverity Scan]

<a href="https://www.clipartof.com/435776"><img align="right" src="logo.png"
   alt="http://toonclips.com/design/788" title="Copyright © Ron Leishman"></a>


Table of Contents
-----------------

* [Introduction](#introduction)
* [Usage](#usage)
* [Features](#features)
* [libwdog API](#libwdog-api)
* [Operation](#operation)
* [Debugging](#debugging)
* [Build & Install](#build--install)
* [Origin & References](#origin--references)
* [Contributing](#contributing)


Introduction
------------

`watchdogd(8)` is an advanced system and process supervisor daemon,
primarily intended for embedded Linux and server systems.  By default it
periodically kicks the system watchdog timer (WDT) to prevent it from
resetting the system.  In its more advanced guise it monitors critical
system resources, supervises the heartbeat of processes, records
deadline transgressions, and initiates a controlled reset if needed.

When a system starts up, `watchdogd` determines the reset cause by
querying the kernel.  In case of system reset, and not power loss, the
reset reason is available already in a file for later analysis by an
operator or network management system (NMS).  This information can in
turn can be used to put the system in an operational safe state, or
non-operational safe state.


### What is a watchdog timer?

Most server and laptop motherboards today come equipped with a watchdog
timer (WDT).  It is a small timer connected to the reset circuitry so
that it can reset the board if the timer expires.  The WDT driver, and
this daemon, periodically "kick" (reset) the timer to prevent it from
firing.

Most embedded systems utilise watchdog timers as a way to automatically
recover from malfunctions: lock-ups, live-locks, CPU overload.  With a
bit of logic sprinkled on top the cause can more easily be tracked down.

The Linux kernel provides a common userspace interface `/dev/watchdog`,
created automatically when the appropriate watchdog driver is loaded.
If your board does not have a WDT, the kernel provides a `softdog.ko`
module which in many cases can be good enough.

The idea of a watchdog daemon in userspace is to run in the background
of your system.  When there is no more CPU time for the watchdog daemon
to run it will fail to "kick" the WDT.  This will in turn cause the WDT
to reboot the system.  When it does `watchdogd` has already saved the
reset reason for your post mortem.

As a background process, `watchdogd` can of course also be used to
monitor other aspects of the system ...


### What can watchdogd do?

Without arguments `watchdogd` runs in the background, monitoring the the
CPU, and as long as there is CPU time it "kicks" the WDT chip (via the
driver).  If `watchdogd` is stopped, or does not get enough CPU time to
run, the WDT will detect this and reboot the system.  This is the normal
mode of operation.

With a few lines in `/etc/watchdogd.conf`, it can also monitor other
aspects of the system, such as:

- Load average
- Memory leaks
- File descriptor leaks
- Process live locks
- Reset counter, warm boots since last power failure

To top things off there is support for periodically calling a generic
script where operators can do housekeeping checks.  For details on how
to configure this, see the watchdogd.conf(5) man page.


Usage
-----

```
watchdogd [-hnsVx] [-f FILE] [-T SEC] [-t SEC] [/dev/watchdog]

Options:
  -f, --config=FILE        Use FILE for daemon configuration
  -n, --foreground         Start in foreground (background is default)
  -s, --syslog             Use syslog, even if running in foreground
  -l, --loglevel=LVL       Log level: none, err, info, notice*, debug
  
  -T, --timeout=SEC        HW watchdog timer (WDT) timeout in SEC seconds
  -t, --interval=SEC       WDT kick interval in SEC seconds, default: 10
  -x, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM
                           "magic" exit may not be supported by HW/driver
  
  -V, --version            Display version and exit
  -h, --help               Display this help message and exit
```

Without any arguments, `watchdogd` opens `/dev/watchdog`, forks to the
background, tries to to set a 20 sec WDT timeout, and then kicks every
10 sec.  See the [Operation](#operation) section for more information.

**Example**

```shell
watchdogd -T 120 -t 30 /dev/watchdog2
```


Features
--------

To force a kernel watchdog reboot, `watchdogd` supports `SIGPWR`, used
by some `init(1)` systems to delegate a reboot.  What it does is to set
the WDT timer to the lowest possible value (1 sec), close the connection
to `/dev/watchdog`, and wait for WDT reboot.  It waits at most 3x the
WDT timeout before announcing HW WDT failure and forcing a reboot.

`watchdogd(8)` supports optional monitoring of several system resources
that can be enabled in the `.conf` file.  First, system load average
monitoring can be enabled with:

```
loadavg {
    interval = 300       # Every 5 mins
    warning  = 1.5
    critical = 2.0
}
```

Second, the memory leak detector, a value of 1.0 means 100% memory use:

```
meminfo {
    interval = 3600       # Every hour
    warning  = 0.9
    critical = 0.95
}
```

Third, file descriptor leak detector:

```
filenr {
    interval = 3600       # Every hour
    warning  = 0.8
    critical = 0.95
}
```

All of these monitors can be *very* useful on an embedded or headless
system with little or no operator.

The two values, `warning` and `critical`, are the warning and reboot
levels in percent.  The latter is optional, if it is omitted reboot is
disabled.  A script can also be run instead of reboot, see the `.conf`
file for details.

Determining suitable system load average levels is tricky.  It always
depends on the system and use-case, not just the number of CPU cores.
Peak loads of 16.00 on an 8 core system may be responsive and still
useful but 2.00 on a 2 core system may be completely bogged down.  Make
sure to read up on the subject and thoroughly test your system before
enabling a reboot trigger value.  `watchdgod` uses an average of the
first two load average values, the one (1) and five (5) minute.  For
more information on the UNIX load average, see this [StackOverflow
question][loadavg].

The RAM usage monitor only triggers on systems without swap.  This is
detected by reading the file `/proc/meminfo`, looking for the
`SwapTotal:` value.  For more details on the underlying mechanisms of
file descriptor usage, see [this article][filenr].  For more info on the
details of memory usage, see [this article][meminfo].

`watchdogd` v2.0 and later comes with a process supervisor (previously
called pmon).  When the supervisor is enabled the daemon runs as a
real-time task with the configured priority, default 98.  Monitored
clients connect to the supervisor using the libwdog API.

```
supervisor {
    enabled = true
    priority = 98
}
```

[See below](#libwdog-api) for details on how to have your process
internal deadlines be supervised.

When a process fails to meet its deadlines, or a monitor plugin reaches
critical level, `watchdogd` initiates a controlled reset.  To see the
reset reason after reboot, the following section must be enabled in the
`/etc/watchdogd.conf` file:

```
reset-reason {
    enabled = true
#   file    = /var/lib/watchdogd.state  # default
}
```

The `file` setting is optional, the default is usually sufficient, but
make sure the destination directory is writable if you change it.  You
can either inspect the file, or use the `watchdogctl` tool.


libwdog API
-----------

To have `watchdogd` supervise a process, it must be instrumented with at
least a "subscribe" and a "kick" API call.  Commonly this is achieved by
adding the `wdog_kick()` call to the main event loop.

All libwdog API functions, except `wdog_ping()`, return POSIX OK(0) or
negative value with `errno` set on error.  The `wdog_subscribe()` call
returns a positive integer (including zero) for the watchdog `id`.

```C
/*
 * Enable or disable watchdogd at runtime.
 */
int wdog_enable      (int enable);
int wdog_status      (int *enabled);

/*
 * Check if watchdogd API is actively responding,
 * returns %TRUE(1) or %FALSE(0)
 */
int wdog_ping        (void);

/*
 * Register with process supervisor, timeout in msec
 * Return value is the `id`, or -1 on error
 */
int wdog_subscribe   (char *label, unsigned int timeout, unsigned int *ack);
int wdog_unsubscribe (int id, unsigned int ack);
int wdog_kick        (int id, unsigned int timeout, unsigned int ack, unsigned int *next_ack);
int wdog_kick2       (int id, unsigned int *ack);
int wdog_extend_kick (int id, unsigned int timeout, unsigned int *ack);
```

It is highly recommended to use an event loop like libev, [libuev][], or
similar.  For such libraries one can simply add a timer callback for the
kick to run periodically to monitor proper operation of the client.


### Example

For other applications, identify your main loop, its max period time and
instrument it like this:

```C
int ack, wid;

/* Library will use process' name on NULL first arg. */
wid = wdog_subscribe(NULL, 10000, &ack);
if (-1 == wid)
        ;      /* Error handling */

while (1) {
        ...
        wdog_kick2(wid, &ack);
        ...
}
```

This example subscribe to the watchdog with a 10 sec timeout.  The `wid`
is used in the call to `wdog_kick2()`, with the received `ack` value.
Which is changed every time the application calls `wdog_kick2()`, so it
is important the correct value is used.  Applications should of course
check the return value of `wdog_subscribe()` for errors, that code is
left out for readability.

See also the [example/ex1.c][ex1] in the source distribution.  This is
used by the automatic tests.


Operation
---------

By default, `watchdogd` forks off a daemon in the background, opens the
`/dev/watchdog` device, attempts to set the default WDT timeout to 20
seconds, and then enters its main loop where it kicks the watchdog every
10 seconds.

If a WDT device driver does not support setting the timeout, `watchdogd`
attempts to query the actual (possibly hard coded) watchdog timeout and
then uses half that time as the kick interval.

When `watchdogd` backgrounds itself, syslog is implicitly used for all
informational and debug messages.  If a user requests to run the daemon
in the foreground `watchdogd` will also log to `STDERR` and `STDOUT`,
unless the user gives the `--syslog` argument to force use of syslog.

The `/etc/watchdogd.conf` file and the command line control toool
`watchdogctl` can be used to enable more features and query status.


Debugging
---------

The code base has `LOG()`, `INFO()` and `DEBUG()` statements almost
everywhere.  Use the `--loglevel=debug` command line option to enable
full debug output to stderr or the syslog, depending on how you start
`watchdogd`.  The default log level is `notice`, which enables `LOG()`,
`WARN()` and error messages.

The `watchdogctl debug` command can be used at runtime to enable the
debug log level, without having to restart a running daemon.


Build & Install
---------------

> **Note:** To enable any of the extra monitors and the process supervisor,
> see `./configure --help`

`watchdogd` is tailored for Linux systems and builds against most modern
C libries.  However, three external libraries are required: [libite][],
[libuEv][], and [libConfuse][].  Neither should present any surprises,
all of them use de facto standard `configure` scripts and support
`pkg-config`.  The latter is used by the `watchdogd` `configure` script
use to locate requried libraries and header files.

Hence, the regular `./configure && make` is usually sufficient to build
`watchdogd`.  But, if libraries are installed in non-standard locations
you may need to provide their paths:

```shell
PKG_CONFIG_PATH=/opt/lib/pkgconfig:/home/ian/lib/pkgconfig ./configure
make
```

If you're not building from a released tarball but instead use the GIT
sources, see the [Contributing](#contributing) section below.


Origin & References
-------------------

`watchdogd(8)` is an improved version of the original, created by
Michele d'Amico and adapted to [uClinux-dist][] by Mike Frysinger.  It
is maintained by [Joachim Nilsson][] collaboratively at [GitHub][].

The [original code][] in uClinux-dist is available in the public domain,
whereas this version is distributed under the ISC license.  See the
file [LICENSE][] for more details on this.

The [logo][], "Watch Dog Detective Taking Notes", is licensed for use by
the `watchdogd` project, copyright © [Ron Leishman][].


Contributing
------------

If you find bugs or want to contribute fixes or features, check out the
code from GitHub:

```shell
git clone https://github.com/troglobit/watchdogd
cd watchdogd
./autogen.sh
```

The `autogen.sh` script runs `autoconf`, `automake`, et al to create the
configure script and such generated files not part of the VCS tree.  For
more details, see the file [CONTRIBUTING][contrib].


[uClinux-dist]:    http://www.uclinux.org/pub/uClinux/dist/
[loadavg]:         http://stackoverflow.com/questions/11987495/linux-proc-loadavg
[filenr]:          http://www.cyberciti.biz/tips/linux-procfs-file-descriptors.html
[meminfo]:         http://www.cyberciti.biz/faq/linux-check-memory-usage/
[original code]:   http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[libite]:          https://github.com/troglobit/libite/
[libuEv]:          https://github.com/troglobit/libuev/
[libConfuse]:      https://github.com/martinh/libconfuse/
[License]:         https://en.wikipedia.org/wiki/ISC_license
[License Badge]:   https://img.shields.io/badge/License-ISC-blue.svg
[Travis]:          https://travis-ci.org/troglobit/watchdogd
[Travis Status]:   https://travis-ci.org/troglobit/watchdogd.png?branch=master
[Coverity Scan]:   https://scan.coverity.com/projects/6458
[Coverity Status]: https://scan.coverity.com/projects/6458/badge.svg
[GitHub]:          http://github.com/troglobit/watchdogd
[ex1]:             https://github.com/troglobit/watchdogd/blob/master/examples/ex1.c
[LICENSE]:         https://github.com/troglobit/watchdogd/blob/master/LICENSE
[contrib]:         https://github.com/troglobit/watchdogd/blob/master/CONTRIBUTING.md
[Joachim Nilsson]: http://troglobit.com
[logo]:            https://www.clipartof.com/435776
[Ron Leishman]:    http://toonclips.com/design/788
