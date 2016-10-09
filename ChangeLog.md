ChangeLog
=========

All notable changes to the project are documented in this file.


[3.0][UNRELEASED]
-----------------

This release includes some major changes to the build system and is
incompatible with previous version due to changes in the command line
options.

### Changes
- Completely overhauled command line options and arguments.  Some
  options in previous releases were not options but optional arguments,
  while others were useless options for a daemon.
  - No more `--logfile=FILE` option, redirect `stderr` instead.
  - When running foreground, output syslog also to `stderr`, unless
	the `--syslog` option is given.
  - XXX: more changes later, e.g. device, safe-exit, etc.

### Fixes
- XXX: Fix outstanding issues found by Coverity Scan


[2.0.1][] - 2016-06-12
----------------------

Minor bugfix release

### Changes
- Update README with simple API example
- Make it possible to run automatic tests as non-root
- Add automatic testing of PMON API to Travis
- Add Coverity Scan

### Fixes
- Silence GNU ar output, has suddenly started warning about `ar crus`.
- Only `write()` to watchdog if descriptor is valid, fixes annoying
  issue with watchdog not being properly disabled with `wdt_enable()`
- Fix issue in `wdt_enable()` which could possible deref. NULL pointer
- Only change enabled state in `wdt_enable()` if operation is succesful


[2.0][] - 2015-09-20
--------------------

This release brings two new plugins and a client side API for process monitoring.

### Changes
- New file descriptor leak monitor plugin, enable with
  `-n, --filenr=VAL`
- New memory leak monitor plugin, enable with
  `m, --meminfo=VAL`
- The loadavg plugin no longer starts automatically, must give
  `-a, --load-average=VAL` command line argument
- Process monitoring plugin with client side instrumentation API
- When connecting to pmon from a monitored client watchdogd now
  raises its priority to `RT_PRIO` 98.  This to ensure proper monitoring
  of processes, which is considered more important than checking if
  there is any CPU slices left.
- When a monitored process misses its deadline the cause is saved
  in `/var/lib/misc/watchdogd.state` so the cause for a reboot can be
  determined at next boot.
- The reboot/reset cause is also saved on SIGPWR, too high loadavg,
  too high memory usage, and file descriptor exhaustion. More on this
  in future releases.
- BusyBox watchdog command line compatibility added.

### Fixes
- Minor cleanup of loadavg plugin
- If the kernel WDT does not reboot the device after 3x timeout watchdogd
  now reboots by itself.


[1.5][] - 2012-11-06
--------------------

This is the last release in the v1.x series. It adds support for:

- `-d <device` to select a different watchdog device node
- `-a <WARN,REBOOT>` to enable loadavg monitoring.  Warn and reboot
  levels are normalized loadavg, no need to take number of CPU cores
  into account.  Feature added by @clockley

The release also inclucdes some clean up and refactoring of the code
base in preparation for the upcoming v2.0 release, which will add
support for process monitoring with an instrumentation API.


[UNRELEASED]: https://github.com/troglobit/watchdogd/compare/2.0...HEAD
[2.0]:        https://github.com/troglobit/watchdogd/compare/1.6...2.0
[1.6]:        https://github.com/troglobit/watchdogd/compare/1.5...1.6

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
