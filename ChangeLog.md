ChangeLog
=========

All notable changes to the project are documented in this file.

[3.2][] - 2019-05-27
--------------------

### Changes
- Issue #17: When the process supervisor is enabled `watchdogd` now
  always runs with elevated RT priority.  Previous releases changed to
  `SCHED_RR` only when the first supervised process connected, and
  conversely disbled RT prio when the last process disconnected.  This
  change gives a more predictable behavior and also means `watchdogd`
  can be relied upon until the system has been properly diagnosed
- If the (optional) supervisor script returns OK (0) the timer for the
  offending process is now disarmed and the system is not rebooted.
- Retry handover from Finit buit-in watchdog if first attempt fails
- New generic script monitor, thanks to Tom Deblauwe.  Can periodically
  call a site specific script, with timeout in case the script hangs

### Fixes
- Fix #16: Only force reboot on exit if `watchdogd` is enabled
- When disabling and the re-enabling `watchdogd` using the API the
  daemon was sometimes stopped by Finit.  This happened because the
  daemon re-issued a watchdog handover signal to Finit.  The fix is
  to only do the handover once.
- When re-enabling `watchdogd` the supervisor was not properly elevating
  the RT priority, instead it remained as a `SCHED_OTHER` process.  This
  fix makes sure to save and re-use the configured RT priority.


[3.1][] - 2018-06-27
--------------------

### Changes
- Supervised processes can now also cause reset if the ACK sequence
  is wrong when kicking or unsubscribing
- Issue #7: Add support for callback script to the process supervisor:
  `script = /path/to/script.sh` in the `supervisor {}` section
  enables it.  When enabled all action is delegated to the script,
  which is called as: `script.sh supervisor CAUSE PID LABEL`.
  For more information, see the manual for `watchdogd.conf`
- A new command 'fail' has been added to `watchdogctl`.  It can be
  used with the supervisor script to record the reset cause and do
  a WDT reset.  The reset `CAUSE` can be forwarded by the script
  to record the correct (or another) reset cause
- Add `-p PID` to `watchdogctl`.  Works with reset and fail commands
- Always warn at startup if driver/WDT does not support safe exit,
  i.e. "magic close"
- Issue #4: Add warning if `.conf` file cannot be found
- Issue #5: Add recorded time of reset to reset cause state file

### Fixes
- Omitting critical/reboot level from a checker plugin causes default
  value of 95% to be set, causing reboot by loadavg plugin.  Fixed by
  defaulting to 'off' for checker/monitor critical/reboot level
- Issue #6: mismatch in label length between supervised processes and
  that in `wdog_reason_t` => increase from 16 to 48 chars
- Issue #11: problem disabling the process supervisor at runtime, it
  always caused a reboot


[3.0][] - 2018-02-10
--------------------

This release includes major changes to both the build system and the
`watchdogd` command line interface, making it incompatible with previous
versions.  Therefore the major version number has been bumped.

Application writes can now ask `pkg-config` for `CFLAGS` and `LIBS` to
use the process supervisor interface in `libwdog.so`

Reset cause is now queried and saved in `/var/lib/watchdogd.state` at
boot.  Use the new `watchdogctl` tool to interact with and query status
from the daemon.

A configuration file, `/etc/watchdogd.conf`, with many more options for
the health monitor plugins, the process supervisor, and the reset cause.

### Changes
- A configuration file, `/etc/watchdogd.conf`, has been added
- A new tool, `watchdogctl`, to interact with daemon has been added
- New official Watch Dog Detective logo, courtesy of Ron Leishman,
  licensed for use with the watchdogd project
- New or updated manual pages for daemon, ctrl tool, and the .conf file
- Health monitor plugins now support running external script instead of
  default reboot action
- Health monitor plugins no longer need critical/reboot level set, only
  warning is required to enable a monitor
- Completely overhauled `watchdogd` command line options and arguments.
  Some options in previous releases were not options but optional
  arguments, while others were useless options for a daemon:
  - Watchdog device node is now an argument not a `-d` option
  - No more `--logfile=FILE` option, redirect `stderr` instead
  - `-n` now prevents the daemon from forking to the background
  - `-f` is now used by the `--config` file option
  - When running in the foreground, output syslog also to `stderr`,
    unless the `-s`, or `--syslog`, option is given
  - `-l, --loglevel` replaces `--verbose` option
  - Use BusyBox options `-T` and `-t` for WDT timeout and kick, this
    replaces the previous `-w` and `-k` options
- No more support for attaching an external supervisor process using
  `SIGUSR1` and `SIGUSR2`
- Conversion to GNU Configure and Build system
- Native support for building Debian packages
- Default install prefix changed, from `/usr/local` to `/`
- Added `pkg-config` support to `libwdog`
- Save reset cause in `/var/lib/watchdogd.state`, by default disabled
  enable with the .conf file
- Possible to disble default reset cause backend and plug in your own.
  See `src/rc.h` for the API required of your own backend
- Updates to `libwdog` API, including a compatiblity mode for current
  customer(s) using `watchdogd` 2.0 with a supervisor patch
- Added `libwdog` example clients
- Added customer specific compat `/var/run/supervisor.status`
- Support for delayed reboot in user API, `wdog_reset_timeout()`
- Fully integrated with Finit, PID 1.  Both `reboot(1)` and reset via
  `watchdogd`, e.g. `watchdogctl reset`, is delegated via Finit to
  properly shut down the system, sync and unmount all file systems
  before delegating the actual reset to the WDT.


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


[UNRELEASED]: https://github.com/troglobit/watchdogd/compare/3.2...HEAD
[3.2]:        https://github.com/troglobit/watchdogd/compare/3.1...3.2
[3.1]:        https://github.com/troglobit/watchdogd/compare/3.0...3.1
[3.0]:        https://github.com/troglobit/watchdogd/compare/2.0.1...3.0
[2.0.1]:      https://github.com/troglobit/watchdogd/compare/2.0...2.0.1
[2.0]:        https://github.com/troglobit/watchdogd/compare/1.6...2.0
[1.6]:        https://github.com/troglobit/watchdogd/compare/1.5...1.6
