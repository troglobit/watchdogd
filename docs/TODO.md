Goal of watchdogd
=================

Monitor the health of the system and its processes.


Before Release
--------------

* Install default `watchdogd.conf`
* Allow script per monitor plugin, may want to run a script on file
  descriptor and memory leaks, e.g. send an SNMP trap or email
* watchdogctl log debug --> log change: "Setting log level debug"
* Add watchdogctl reload to initiate SIGHUP with feedback when done
* rename reboot --> reset, reboot is the command
* When checker reboots, log to syslog as well, RC may be disabled


General
-------

* Checkers
  - Check if process table is full, i.e. try fork()
  - Check temperature sensor
  - Ping, with optional outbound iface, script to run if ping fails
  - Custom script, run operator provided checker
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
