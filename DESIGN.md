Theory of Operation
-------------------

At runtime `watchdogd` runs as a regular priority task, periodically it
sends a `WDIOC_KEEPALIVE` command to the driver.  As soon as it misses
its deadline the underlying WDT will enforce a system reset.

```
(state)  /var/lib/watchdogd.state  |
(status) /run/watchdogd.status     V
,,,,,                           .status
[WDT]    WatchDog Timer         exists? ----------. yes, read
`````                              |              | (status)
                                   |      ,,,,,   |
                                 What? <--[WDT]   |
                                   |      `````   |
                                   |              |
          .---------- (status)<-- Who? <--(state) |
          |                        |         ^    |
          |                        |         |    |
          | status²             Prepare -----'    |
          v                    WDT reset          |
    watchdogctl                    |              |
         |^                        |<-------------'
         ||                        |
         ||                  set timeout----.
         ||                        |        |
         ||         status¹        |        V
         |'--------------------  loop <-. ,,,,,
         '---------------------> wait   | [WDT]
                    status?        '----' `````
                                Periodic WDT kick

    Fig. 1: Principal operation of watchdogd
```

When `watchdogd` starts it queries the WDT for the reset cause flags and
then proceeds to read the (state) file from non-volatile store.  This
was created before reset by `watchdogd` and contains more detailed info
on the cause of the reset, see Fi. 2 below for an overview.  The data
collected is the stored in a (status) file, which can be volatile store.
Should the daemon be restarted at runtime, for whatever reason, it can
quickly pick up where it left of by checking for the existence of this
(status) file.  Before entering the main loop, the daemon sets the WDT
timeout and opens a client socket.  In the main loop the daemon kicks
the WDT periodically and responds to client requests.

For each reboot `watchdogd` maintains a reset counter, along with the
data collected in the (state) file.  This reset counter is incremeted in
the `Prepare WDT reset` state in Fig 1.  Hence, depending on the
integrity class of the non-volatile store, this counter can be used as
the snmpEngineBoots (RFC 2574) value, and the `sysinfo()` uptime value
can be used as the snmpEngineTime.

The status can be read either from the file (status²), or by using the
client API, like `watchdogctl` which returns (status¹).

When the process supervisor/monitor (PMON) is enabled operation changes
slightly.  This is the mode where services can register with `watchdogd`
to monitor their internal process loop.  Each service registers with a
period time and a name, and then promises to periodically send keepalive
messages.  As soon as the first service registers `watchdogd` elevates
its own priority to ensure no priority inversion occurs for its now
critical supervisory role.  If a service fails to meet its deadline
`watchdogd` records this in the (state) file, in non-volatile store,
followed by a forced WDT reboot.  When the system comes back up again
`watchdogd` reads back the (state) file and stores the information in
the (status) file for offline analysis and sends it to syslog.  An NMS
can query this information when needed.

When the system starts up (1) `watchdogd` first tries to ascertain why
the system is starting up.  The below sequence diagram is used and each
of the states below are possible end states, including (WDT).  In fact
the (WDT) state is the default cause stored by `watchdogd` in the log in
case of CPU overload.

```
        (1)
     ____|_____
    |          |
(PWR Fail)   (WDT)
          _____|_____
         |           |
     (PID Fail)   (Reboot)

Fig. 2: Reset cause FTA
```


Recommendations
---------------

Primarily you need to be able to distinguish between a power failure
(PWR Fail) and a WDT timeout.  The latter can be caused by one of two
things: a process misses its deadline or a user issuing reboot.  The
former of these two can be a monitored process or watchdogd itself
failing to kick the WDT driver.

On systems where you cannot distinguish between a power failure and a
WDT timeout it is impossible to provide adequate information to an NMS.
When possible, opt for a more expensive WDT chipset with voltage
monitoring.  For embedded systems designers, hardware engineers in
particular, always make sure to connect the RESETn signal of the WDT to
all relevant circuitry to ensure a WDT reset set the HW in the same
state as after a regular PWR ON.

In kernel driver terms, lokk for a chipset + driver supporting these
flags: `WDIOF_SETTIMEOUT | WDIOF_POWERUNDER | WDIOF_CARDRESET |
WDIOF_KEEPALIVEPING`

This one is also useful, but not crucial: `WDIOF_MAGICCLOSE`

