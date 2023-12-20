Goal of watchdogd
=================

Monitor the health of the system and its processes.


System Health Monitor
---------------------

Add system health monitor with capabilities to monitor:

* Temperature sensor
* Network connectivity, e.g. ping with optional outbound iface and a
  script to run if ping (three attempts) fails
* RAM disks used for log files.  Best way is probably to implement this
  as a generic checker that the user can define any way they like.  E.g,

        fs-monitor /var { warning = 90%, critical 95% }

  Use the C library API statfs(2).
