Goal of watchdogd
=================

Monitor the health of the system and its processes.


Misc
----

* Support `-i IDENT` in case of running multiple watchdog daemons.
  This would then change the process name, pid file, and syslog tag.


System Health Monitor
---------------------

Add system health monitor with capabilities to monitor:

* Network connectivity, e.g. ping with optional outbound iface and a
  script to run if ping (three attempts) fails -- for ideas, see:
  https://troglobit.com/post/2023-01-15-finit-custom-connectivity-check/
* Add `contrib/` for all the generic scripts used for testing with Infix
