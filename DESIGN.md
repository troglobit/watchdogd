Theory of Operation
-------------------

At runtime `watchdogd` runs as a regular priority task, periodically it
sends a `WDIOC_KEEPALIVE` command to the driver.  As soon as it misses
its deadline the underlying WDT will enforce a system reset.

When PMON is enabled operation changes slightly.  This is the process
monitor/supervisor mode where we allow services to register with us to
monitor their internal process loop.  A service registers with a period
time and a name, periodically sending keepalive messages.  As soon as
the first service registers `watchdogd` elevates its own priority to
ensure no priority inversion occurs for its now critical supervisory
role.  If a service fails to meet its deadline `watchdogd` records this
in a log and then forces a WDT reboot.  When the system comes back up
again `watchdogd` reads back the log, stores the information for offline
analysis and sends it to syslog.  An NMS can query this information when
needed.

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

