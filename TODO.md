Goal of watchdogd
=================

Monitor the health of the system and its processes.


General
-------

* Support enable/disable watchdog features in a `/etc/watchdogd.conf`
  - Supervise processes,
  - CPU loadavg,
  - Watch for file descriptor leaks,
  - etc.
* Save reset reason to persistent backend store
  - Reason: 0-255 (0: Power-ON, 1: SW-Reboot, 2: Pwr-Fail, 5-255 Proc ID)
  - Hard disk/flash
  - RTC alarm register


System Health Monitor
---------------------

Add system health monitor with capabilities to monitor:

* CPU loadavg.  For details on UNIX loadavg, see
  http://stackoverflow.com/questions/11987495/linux-proc-loadavg
* Default to loadavg 0.7 as MAX recommended load before warning and 0.9
  reboot, with one core.
* File descriptor leaks, warn and reboot
* RAM usage
* RAM disks used for logfiles


Process Supervision
-------------------

Add a system supervisor (pmon) to optionally reboot the system when a
process stops responding, or when a respawn limit has been reached, 

* API for processes to register with the watchdog, libwdt Deeper
  monitoring of a process' main loop by instrumenting
* If a process is not heard from within its subscribed period time
  reboot system or restart process.
* Add client libwdt library, inspired by old Westermo API and the
  [libwdt] API published in the [Fritz!Box source dump], take for
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
