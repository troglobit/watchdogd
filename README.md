System & Process Supervisor for Linux
=====================================
[![License Badge][]][License] [![GitHub Status][]][GitHub] [![Coverity Status][]][Coverity Scan]

<a href="https://www.clipartof.com/435776"><img align="right" src="./doc/logo.png"
   alt="http://toonclips.com/design/788" title="Copyright © Ron Leishman"></a>

Table of Contents
-----------------

* [Introduction](#introduction)
* [Features](doc/features.md)
  - [Delegating Reboot](doc/features.md#delegating-reboot)
  - [Built-in Monitors](doc/features.md#built-in-monitors)
  - [Generic Script](doc/features.md#generic-script)
  - [Process Supervisor](doc/features.md#process-supervisor)
* [Advanced Usage](doc/advanced.md)
  - [Debugging](doc/advanced.md#debugging)
  - [libwdog API](doc/advanced.md#libwdog-api)
* [Build & Install](#build--install)
* [Contributing](#contributing)
* [Origin & References](#origin--references)


Introduction
------------

[watchdogd(8)][] is an advanced system and process supervisor daemon,
primarily intended for embedded Linux and server systems.  By default it
periodically kicks the system watchdog timer (WDT) to prevent it from
resetting the system.  In its more advanced guise it monitors critical
system resources, supervises the heartbeat of processes, records
deadline transgressions, and initiates a controlled reset if needed.

When a system starts up, `watchdogd` determines the reset cause by
querying the kernel.  In case of system reset, and not power loss, the
reset reason is available already in a file for later analysis by an
operator or network management system (NMS).  This information in
turn can be used to put the system in an operational safe state, or
non-operational safe state.

> **News:** as of v4.0, multiple watchdog devices are supported.


### What is a watchdog timer?

Most server and laptop motherboards today come equipped with a watchdog
timer (WDT).  It is a small timer connected to the reset circuitry so
that it can reset the board if the timer expires.  The WDT driver, and
this daemon, periodically "kick" (reset) the timer to prevent it from
firing.

Most embedded systems utilize watchdog timers as a way to automatically
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

Without arguments `watchdogd` runs in the background, monitoring the CPU
and as long as there is CPU time it "kicks" `/dev/watchdog` every 10
seconds.  If the daemon is stopped, or does not get enough CPU time to
run, the underlying WDT hardware will detect this and reboot the system.
This is the normal mode of operation.

With a few lines in [watchdogd.conf(5)][], it can also monitor other
aspects of the system, such as:

- File descriptor leaks
- File system usage
- Generic script
- Load average
- Memory leaks
- Process live locks
- Reset counter, e.g., for snmpEngineBoots (RFC 2574)
- Temperature

Read more about [Built-in Monitors](doc/features.md#built-in-monitors)
in the extended documentation.


Build & Install
---------------

> **Note:** To enable any of the extra monitors and the process supervisor,
> see `./configure --help`

`watchdogd` is tailored for Linux systems and builds against most modern
C libraries.  However, three external libraries are required: [libite][],
[libuEv][], and [libConfuse][].  Neither should present any surprises,
all of them use de facto standard `configure` scripts and support
`pkg-config`.  The latter is used by the `watchdogd` `configure` script
use to locate required libraries and header files.

The common `./configure --some --args --here && make` is usually
sufficient to build `watchdogd`.  But, if libraries are installed in
non-standard locations you may need to provide their paths, e.g. with
`PKG_CONFIG_PATH`.  The following also sets the most common install
and search paths for the build:

```shell
PKG_CONFIG_PATH=/opt/lib/pkgconfig:/home/ian/lib/pkgconfig \
    ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var
make
```

If you're not building from a released tarball but instead use the GIT
sources, see the [Contributing](#contributing) section below.


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
more details, see the file [CONTRIBUTING][contrib] in the GIT sources.


Origin & References
-------------------

[watchdogd(8)[] is an improved version of the original, created by
Michele d'Amico and adapted to [uClinux-dist][] by Mike Frysinger.  It
is maintained by [Joachim Wiberg][] collaboratively at [GitHub][].

The [original code][] in uClinux-dist is available in the public domain,
whereas this version is distributed under the ISC license.  See the
file [LICENSE][] for more details on this.

The [logo][], "Watch Dog Detective Taking Notes", is licensed for use by
the `watchdogd` project, copyright © [Ron Leishman][].


[uClinux-dist]:      http://www.uclinux.org/pub/uClinux/dist/
[original code]:     http://www.mail-archive.com/uclinux-dev@uclinux.org/msg04191.html
[libite]:            https://github.com/troglobit/libite/
[libuEv]:            https://github.com/troglobit/libuev/
[libConfuse]:        https://github.com/martinh/libconfuse/
[License]:           https://en.wikipedia.org/wiki/ISC_license
[License Badge]:     https://img.shields.io/badge/License-ISC-blue.svg
[GitHub]:            https://github.com/troglobit/watchdogd/actions/workflows/build.yml/
[GitHub Status]:     https://github.com/troglobit/watchdogd/actions/workflows/build.yml/badge.svg
[Coverity Scan]:     https://scan.coverity.com/projects/6458
[Coverity Status]:   https://scan.coverity.com/projects/6458/badge.svg
[GitHub]:            http://github.com/troglobit/watchdogd
[LICENSE]:           https://github.com/troglobit/watchdogd/blob/master/LICENSE
[contrib]:           https://github.com/troglobit/watchdogd/blob/master/.github/CONTRIBUTING.md
[Joachim Wiberg]:    http://troglobit.com
[logo]:              https://www.clipartof.com/435776
[Ron Leishman]:      http://toonclips.com/design/788
[watchdogd(8)]:      https://man.troglobit.com/man8/watchdogd.8.html
[watchdogd.conf(5)]: https://man.troglobit.com/man5/watchdogd.conf.5.html
