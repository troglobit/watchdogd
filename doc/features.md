Features
========


Delegating Reboot
-----------------

To force a kernel watchdog reboot, `watchdogd` supports `SIGPWR`, used by some
[init(1)][] systems to delegate a reboot.  What it does is to set the WDT timer
to the lowest possible value (1 sec), close the connection to `/dev/watchdog`,
and wait for WDT reboot.  It waits at most 3x the WDT timeout before
announcing HW WDT failure and forcing a reboot.


Built-in Monitors
-----------------

[watchdogd(8)][] supports optional monitoring of several system resources that
can be enabled in [watchdogd.conf(5)][].

All of these monitors can be *very* useful on an embedded or headless
system with little or no operator supervision.

The two values, `warning` and `critical`, are the warning and reboot
levels in percent.  The latter is optional, if it is omitted reboot is
disabled.  A script can also be run instead of reboot, see the `.conf`
file for details.

Determining suitable system load average levels is tricky.  It always
depends on the system and use-case, not just the number of CPU cores.
Peak loads of 16.00 on an 8 core system may be responsive and still
useful but 2.00 on a 2 core system may be completely bogged down.  Make
sure to read up on the subject and thoroughly test your system before
enabling a reboot trigger value.  `watchdogd` uses an average of the
first two load average values, the one (1) and five (5) minute.  For
more information on the UNIX load average, see this [StackOverflow
question][loadavg].

The RAM usage monitor only triggers on systems without swap.  This is
detected by reading the file `/proc/meminfo`, looking for the
`SwapTotal:` value.  For more details on the underlying mechanisms of
file descriptor usage, see [this article][filenr].  For more info on the
details of memory usage, see [this article][meminfo].


### System Load

System load average that can be monitored with:

```
loadavg {
    enabled  = true
    interval = 300       # Every 5 mins
	logmark  = true
    warning  = 1.5
    critical = 2.0
}
```

By enabling output in syslog, using `logmark = true`, you can set up the
system to forward the monitored resources to a remote syslog server.  The
syslog output for load average looks like this:

    watchdogd[2323]: Loadavg: 0.32, 0.07, 0.02 (1, 5, 15 min)

### Memory Usage

The memory leak detector, a value of 1.0 means 100% memory use:

```
meminfo {
    enabled  = true
    interval = 3600       # Every hour
	logmark  = true
    warning  = 0.9
    critical = 0.95
}
```

The syslog output looks like this:

    watchdogd[2323]: Meminfo: 59452 kB, cached: 23912 kB, total: 234108 kB

### File Descriptor Usage

File descriptor leak detector:

```
filenr {
    enabled  = true
    interval = 3600       # Every hour
	logmark  = true
    warning  = 0.8
    critical = 0.95
}
```

The syslog output looks like this:

    watchdogd[2323]: File nr: 288/17005


### File System Usage

Currently only a single file system can be monitored, in this example we
monitor `/var` every five minutes.

```
fsmon /var {
    enabled  = true
    interval = 300       # Every five minutes
	logmark  = true
    warning  = 0.8
    critical = 0.95
}
```

The syslog output looks like this:

    watchdogd[2323]: Fsmon /var: blocks 404/28859 inodes 389/28874


Generic Script
--------------

In addition to the built-in monitors, there is support for periodically
calling a generic script where operators can do housekeeping checks. 

```
generic {
    enabled = true
    interval = 300
    timeout = 60
    warning  = 1
    critical = 10
    monitor-script = "/path/to/monitor-script.sh"
}
```

For more about this, see [watchdogd.conf(5)][].


Process Supervisor
------------------

`watchdogd` v2.0 and later comes with a process supervisor (previously
called pmon).  When the supervisor is enabled, and the priority is set
to a value > 0, the daemon runs as a real-time task with the configured
priority.  Monitored clients connect to the supervisor using the libwdog
API (see below).

```
supervisor {
    enabled = true
    priority = 98
}
```

> **Note:** Linux cgroup v2 do not support realtime tasks in sub-groups.

[See API section](advanced.md#libwdog-api) for details on how to have
your process internal deadlines be supervised.

When a process fails to meet its deadlines, or a monitor plugin reaches
critical level, `watchdogd` initiates a controlled reset.  To see the
reset reason after reboot, the following section must be enabled in the
`/etc/watchdogd.conf` file:

```
reset-reason {
    enabled = true
#   file    = /var/lib/misc/watchdogd.state  # default
}
```

The `file` setting is optional, the default is usually sufficient, but
make sure the destination directory is writable if you change it.  You
can either inspect the file, or use the [watchdogctl(1)][] tool.

[init(1)]:           https://man.troglobit.com/man8/finit.8.html
[loadavg]:           http://stackoverflow.com/questions/11987495/linux-proc-loadavg
[filenr]:            http://www.cyberciti.biz/tips/linux-procfs-file-descriptors.html
[meminfo]:           http://www.cyberciti.biz/faq/linux-check-memory-usage/
[watchdogd(8)]:      https://man.troglobit.com/man8/watchdogd.8.html
[watchdogctl(1)]:    https://man.troglobit.com/man1/watchdogd.1.html
[watchdogd.conf(5)]: https://man.troglobit.com/man5/watchdogd.conf.5.html
