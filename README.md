Watchdog with loadavg monitoring
================================

Table of Contents
-----------------

* [Introduction](#introduction)
* [Download](#download)
* [Usage](#usage)
* [Features](#features)
* [Operation](#operation)
* [Debugging](#debugging)
* [Origin & References](#origin--references)
* [Contributing](#contributing)


Introduction
------------

This is a refactored and improved version of the original watchdogd from
[uClinux-dist][].  It was written by Michele d'Amico and later adapted
to uClinux-dist by Mike Frysinger.

**Example**

    watchdogd -d /dev/watchdog2 -a 0.8,0.9 -w 120 -k 30

Most WDT drivers only support 120 sec as lowest timeout, but watchdogd
tries to set 20 sec timeout.  Example values above are recommendations

watchdogd runs at the default UNIX priority (nice) level.


Download
--------

Although the project makes heavy use of GitHub, do *not* use the ZIP
file links GitHub provides.  Instead, use the FTP or releases page to
download tarballs:

- http://ftp.troglobit.com/watchdogd/

If you want to [contribute][contrib], check out the code from GitHub
like this, including the submodules.  Remember to update the submodules
whenever you do a `git pull`.

	git clone https://github.com/troglobit/watchdogd
	cd watchdogd
	git submodule update --init

The GitHub download links, including the ZIP files on the releases page,
do not include the files from the GIT submodules, unfortunately.  This
has been reported to GitHub but has not been fixed by them yet.


Usage
-----

    watchdogd [-fxLsVvh] [-d /dev/watchdog] [-a WARN,REBOOT] [-w SEC] [-k SEC]
    
    Options:
      -d, --device=<dev>       Device to use, default: /dev/watchdog
      -f, --foreground         Start in foreground (background is default)
      -x, --external-kick[=N]  Force external watchdog kick using SIGUSR1
                               A 'N x <interval>' delay for startup is given
      -l, --logfile=<file>     Log to <file> in background, otherwise silent
      -L, --syslog             Use syslog, even if in foreground
      -w, --timeout=<sec>      Set the HW watchdog timeout to <sec> seconds
      -k, --interval=<sec>     Set watchdog kick interval to <sec> seconds
      -s, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM
      -a, --load-average=<val> Enable load average check <WARN,REBOOT>
      -V, --verbose            Verbose, noisy output suitable for debugging
      -v, --version            Display version and exit
      -h, --help               Display this help message and exit
    
By default, watchdogd opens `/dev/watchdog`, attempts to set 20 sec WDT
timeout and then kicks, in the background, every 10 sec.


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

To force a kernel watchdog reboot, watchdogd supports `SIGPWR`.  What
it does is to set the WDT timer to the lowest possible value (1 sec),
close the connection to `/dev/watchdog`, and wait for WDT reboot.

System load average monitoring can be enabeled with the `-a 0.8,0.9`
command line argument.  The two values, separated by a comma, is the
normalized load level for logging a warning message and issuing a
reboot, respectively.  Normalized means watchdogd does not care how many
CPU cores your system as online.  If the Linux kernel `/proc/loadavg`
file shows `3.9 3.0 2.5` on a four-core CPU, watchdogd will consider
this as a load of `0.98 0.75 0.63`, i.e. divided by four.  Only the one
(1) and five (5) minute average values are used.  For more information
on the UNIX load average, see this [StackOverflow question][loadavg].

watchdogd also monitors file descriptor usage.  This is currently not
possible to disable.  (Support for that will be in watchdogd 2.0 which
also will use a configuration file.)  The default is to issue a warning
at 80% usage of all available file descriptors, and reboot at 95%.  For
more details on the underlying mechanisms, see [this article][filenr].


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

The [original code][] in uClinux-dist has no license and is available in
the public domain, whereas this version is distributed under the ISC
license.  See the file [LICENSE][] for more on this.


Contributing
------------

This project is maintained by [Joachim Nilsson][] collaboratively at
[GitHub][].  If you find bugs or want to contribute fixes or features,
see the file [CONTRIBUTING.md][contrib] for details.


[uClinux-dist]:    http://www.uclinux.org/pub/uClinux/dist/
[loadavg]:         http://stackoverflow.com/questions/11987495/linux-proc-loadavg
[filenr]:          http://www.cyberciti.biz/tips/linux-procfs-file-descriptors.html
[original code]:   http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[GitHub]:          http://github.com/troglobit/watchdogd
[LICENSE]:         https://github.com/troglobit/watchdogd/blob/master/LICENSE
[contrib]:         https://github.com/troglobit/watchdogd/blob/master/CONTRIBUTING.md
[Joachim Nilsson]: http://troglobit.com

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
