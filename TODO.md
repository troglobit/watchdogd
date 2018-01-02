Goal of watchdogd
=================

Monitor the health of the system and its processes.

Before Release
--------------

* Sample average to not trigger reboot during peak loads


General
-------

* Support enable/disable watchdog features in a `/etc/watchdogd.conf`
  - Supervise processes,
  - CPU loadavg,
  - Watch for file descriptor leaks,
  - etc.
* Save reset reason to persistent backend store
  - Reason: 0-255 (0: Power-ON, 1: SW-Reboot, 2: Pwr-Fail, 4: Pmon)
    In the case of (4) the offending process ID (string or octet ID)
    shall be possible to read out as well.
  - Hard disk/flash
  - RTC alarm register
* watchdogctl:
  - add set timeout command
  - add commands to enable/disable plugins


System Health Monitor
---------------------

Add system health monitor with capabilities to monitor:

* RAM disks used for log files.  Best way is probably to implement this
  as a generic checker that the user can define any way they like.  E.g,

        fs-monitor /var { warning = 90%, critical 95% }

  Use the C library API statfs(2).


Process Supervision
-------------------

Add a system supervisor (pmon) to optionally reboot the system when a
process stops responding, or when a respawn limit has been reached, 

    typedef struct {
        pid_t    pid;          /* Process ID, pid_t from wdog API */
        char     pname[32];    /* Process name, __progname from wdog API */
        uint32_t ptimeout;     /* Process response time */
        uint32_t pack;         /* Next ACK to be sent by process */
    
        /* Written to ext. reset cause register */
        char     pcode[32];    /* Saved to flash, if available. */
        #define  id pcode[0]   /* Saved to RTC alarm */
    } pmon_t;
    
    wdt_pmon_ping()            /* Check if pmon/watchdogd is running. */
    wdt_pmon_subscribe()       /* Register process with pmon */
    wdt_pmon_unsubscribe()     /* Deregister, used on ordered exit() */
    wdt_pmon_kick()            /* Periodic kick, may change ptimeout */
    wdt_pmon_reboot()          /* Watchdog reboot from process */
    
    /* pause pmon as well on wdog pause, reset pmon timers on resume! */
    wdt_pause()

* Document API, maybe try Doxygen this time?
* The pmon API must be non-blocking!  Only the two (un)subscribe API's
  may block for a short period of time to ascertain connectivitiy.
* The pmon API must handle the case when pmon has been stopped, or
  not started yet.  Possibly use 3x 2 sec sleep before timing out
  a subscribe and a single 2 sec sleep timeout on unsubscribe.
  * In the case of subscribe we can assume pmon has not yet started,
    but may start soon -- since the process is set to use pmon.
  * In the case of unsubscribe pmon may suddenly have terminated.  The
    wdog API must check if someone has done `wdt_pause()` before it
    returns an error code to the process.
  * The kick API must not block!  It should use the public ping API
    to check connetivity with pmon before returning error.	
* The ping API must be implemented as non-blocking!
* If some other process has paused the watchdog the kick API must
  continue to operate seamlessly to the process.

* When writing extended reset cause, first read any old value.  Only
  write if new value differs from old value!  This to eliminate any
  unnecessary erase cycles.

* API for processes to register with the watchdog, libwdt Deeper
  monitoring of a process' main loop by instrumenting
* If a process is not heard from within its subscribed period time
  reboot system or restart process.
* Add client libwdt library, inspired by old Westermo API and the
  [libwdt][] API published in the [Fritz!Box source dump][], take for
  instance the `fritzbox7170-source-files-04.87-tar.gz` drop and see the
  `GPL-release_kernel.tgz` in `drivers/char/avm_new/` for the kernel
  driver, and the `LGPL-GPL-release_target_tools.tgz` in `wdt/` for the
  userland API.
* When a client process (a standard app/daemon instrumented with libwdt
  calls in its main loop) registers with the watchdog, we raise the RT
  priority to 98 (just below the kernel watchdog in prio).  This to
  ensure that system monitoring goes before anything else in the system.
* Use UNIX domain sockets for communication between daemon and clients

[libwdt]:                http://www.wehavemorefun.de/fritzbox/Libwdt.so
[Fritz!Box source dump]: ftp://ftp.avm.de/fritz.box/fritzbox.fon_wlan_7170/x_misc/opensrc/

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
