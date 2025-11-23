ChangeLog
=========

All notable changes to the project are documented in this file.


[4.1][UNRELEASED]
--------------

### Changes

- Add `watchdogctl list-clients` command to display currently subscribed
  clients to the process supervisor.  Outputs to stdout in either table
  format (default) with colored headers, or JSON format with `-j/--json`
- New global `-j, --json` option for machine-readable output, currently
  supported by `list-clients` and `status` commands
- New API: `wdog_clients()` returns array of `wdog_client_t` structs for
  programmatic access to subscribed clients.  See API documentation at
  <https://codedocs.xyz/troglobit/watchdogd/wdog_8h.html>
- Enhance `watchdogctl status` command to display formatted output by
  default, with device information, capabilities, and reset history in
  a human-readable table format.  Use `-j/--json` for JSON output


[4.0][] - 2024-01-04
--------------------

> **Breaking changes:** the `generic` script monitor has new syntax, the
> status files have moved, and the format has changed.  Also, the
> default value for `safe-exit` in the .conf file has been changed.

### Changes
- Support for multiple watchdog devices added, issue #26
- The format of `watchdogctl status` and `/run/watchdogd/status` has
  been changed to JSON and includes more information about the currently
  running daemon and the capabilities of watchdog devices in use
- The `configure --with-$MONITOR=SEC` flag has been changed to not
  take an argument (this was never used).  To change the poll interval
  of a system monitor, use the configuration file
- A new file system monitor: `fsmon /var { ... }`, multiple monitors,
  `fsmon /path`, are supported
- A new temperature monitor: `tempmon /path/to/sensor {...}`.  It
  supports multiple sensors, both thermal and hwmon type.  See the
  documentation for details
- The syntax for the generic monitor script has changed.  This is a
  breaking change, everyone must update.  New syntax:

        generic /path/to/montor-script.sh { ... }

- The generic scripts monitor now supports running multiple scripts
- Documentation of the libwdog supervisor API by Andreas Helbech Kleist
- API docs at <https://codedocs.xyz/troglobit/watchdogd/wdog_8h.html>
- State file location changed from `/var/lib/` to `/var/lib/misc/`.
  This is the recommended location in the Linux FHS, and what most
  systems use.  Both the default `watchdogd.conf` and documentation has
  been updated.  Unless a file is specified by the user, the daemon will
  automatically relocate to the new location at runtime.  If the new
  directory does not exist, the daemon will fall back to use the old
  path, if it exists, issue #36
- The default `watchdogd.conf` now enables reset reason by default.
  This is a strong recommendation since it is then possible to trace
  the reset cause also for system monitors
- Simplified README by splitting it into multiple files, some text even
  moved entirely to man pages instead
- The status files cluttering up `/run` have been moved to their own
  subdirectory, `/run/watchdogd`.  This includes the PID file, last boot
  status, and the socket for `watchdogctl`.  The latter remains the
  recommended tool to query status and interact with the daemon
- The configure script flags for enabling system monitors have been
  simplified.  None of the monitors take an argument (poll seconds),
  this because that is configured in `watchdogd.conf`

### Fixes
- Fix #28: `watchdogd` crash in case "Label" or "Reset date" field in
  reset reason is empty.  Found and fixed by Christian Theiss
- Fix #30: replace Finit compile-time detection with runtime check, this
  allows synchronized reboot using `watchdogd` with Finit in Buildroot
- Fix #39: generic monitoring script with runtime > 1 second cause
  system to reboot.  Found and fixed by Senthil Nathan Thangaraj
- Fix #41: calling custom supervisor script cause `watchdogd` to disable
  monitoring, regardless of script exit code.
- Fix #43: `watchdogctl clear`, and `wdog_reset_reason_clr()` API, does
  not work.  Regression introduced in v3.4.
- The generic script plugin can now be disabled at runtime.  Prior to
  this release, it was not possible when once enabled.
- The label (cause) of the system monitor forcing a reset is now saved in
  the reset reason file.  Previously only "forced reset" was the only
  message, which without persistent logs did not say much.


[3.5][] - 2021-12-02
--------------------

Minor compat release; integration with Finit and new libite.

### Changes
  * Migrate from Travis-CI to GitHub Actions
  * Use SIGTERM to signal PID 1, SIGINT Stops working in Finit v4.1
  * Updated examples and manual page(s) with new 'enabled' setting
  * Updated README with exact build example for correct paths
  * Add support for new libite namespace, as of libite v2.5.0


[3.4][] - 2021-04-30
--------------------

### Changes
- Clarify nomenclature: reset cause vs. reset reason
- Change layout and formatting of `watchdogctl` status output
- Change defaults for supervisor, still disabled by default but now
  also with priority set to zero by default.  This allows running
  the supervisor in cgroups v2 systems without realtime priority.

### Fixes
- Fix missing pidfile touch on `SIGHUP`
- Fix problem with plugins being enabled (but incomplete) by default.
  Now all sections have an `enabled = [true|false]` setting, and
  all are disabled by default.  You need to uncomment *end* enable.


[3.3][] - 2020-01-05
--------------------

### Changes
- Increased severity of syslog messages preceding reboot, instead of
  `LOG_ERROR` all messages that result in a reboot use `LOG_EMERG`
  because many `syslogd` services default to log emerg to console
- Add handy summary of options to `configure` script

### Fixes
- Fix possible garbled `next_ack` for users of `libwdog` due to badly
  handled timeout in `poll()` when connecting to `watchdogd`
- Fix `configure` script defaults for the following settings:
  - `--enable-compat`, was always enabled
  - `--enable-exampels`, were always enabled
  - `--enable-syslog-mark`, was always enabled
- Fix use-after-free bug in new script monitor, introduced in v3.2


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
- Only change enabled state in `wdt_enable()` if operation is successful


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


[UNRELEASED]: https://github.com/troglobit/watchdogd/compare/4.0..HEAD
[4.1]:        https://github.com/troglobit/watchdogd/compare/4.0...4.1
[4.0]:        https://github.com/troglobit/watchdogd/compare/3.5...4.0
[3.5]:        https://github.com/troglobit/watchdogd/compare/3.4...3.5
[3.4]:        https://github.com/troglobit/watchdogd/compare/3.3...3.4
[3.3]:        https://github.com/troglobit/watchdogd/compare/3.2...3.3
[3.2]:        https://github.com/troglobit/watchdogd/compare/3.1...3.2
[3.1]:        https://github.com/troglobit/watchdogd/compare/3.0...3.1
[3.0]:        https://github.com/troglobit/watchdogd/compare/2.0.1...3.0
[2.0.1]:      https://github.com/troglobit/watchdogd/compare/2.0...2.0.1
[2.0]:        https://github.com/troglobit/watchdogd/compare/1.6...2.0
[1.6]:        https://github.com/troglobit/watchdogd/compare/1.5...1.6
