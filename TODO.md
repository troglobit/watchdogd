Goal of watchdogd
=================

Monitor the health of the system and its processes.

Before Release
--------------

* Disable reset reason state file by default, enable in .conf
* Support enable/disable watchdog features in a `/etc/watchdogd.conf`
  - command line options win over .conf, even on SIGHUP
  - Supervise processes,
  - CPU loadavg,
  - Watch for file descriptor leaks,
  - etc.


General
-------

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
