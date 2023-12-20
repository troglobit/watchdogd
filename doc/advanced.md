Advanced Usage
==============

Debugging
---------

The code base has `LOG()`, `INFO()` and `DEBUG()` statements almost
everywhere.  Use the `--loglevel=debug` command line option to enable
full debug output to stderr or the syslog, depending on how you start
`watchdogd`.  The default log level is `notice`, which enables `LOG()`,
`WARN()` and error messages.

The `watchdogctl debug` command can be used at runtime to enable the
debug log level, without having to restart a running daemon.


<a href="https://codedocs.xyz/troglobit/watchdogd/wdog_8h.html"><img
   align="right"  src="api.png" alt="API" title="API docs"></a>

libwdog API
-----------

To have `watchdogd` supervise a process, it must be instrumented with at
least a "subscribe" and a "kick" API call.  Commonly this is achieved by
adding the `wdog_kick()` call to the main event loop.

🕮 <https://codedocs.xyz/troglobit/watchdogd/wdog_8h.html>

### Example

For other applications, identify your main loop, its max period time and
instrument it like this:

```C
int ack, wid;

/* Library will use process' name on NULL first arg. */
wid = wdog_subscribe(NULL, 10000, &ack);
if (-1 == wid)
        ;      /* Error handling */

while (1) {
        ...
        /* Ensure kick is called periodically, < 10 sec */
        wdog_kick2(wid, &ack);
        ...
}
```

This example subscribe to the watchdog with a 10 sec timeout.  The `wid`
is used in the call to `wdog_kick2()`, with the received `ack` value.
Which is changed every time the application calls `wdog_kick2()`, so it
is important the correct value is used.  Applications should of course
check the return value of `wdog_subscribe()` for errors, that code is
left out for readability.

See also the [example/ex1.c][ex1] in the source distribution.  This is
used by the automatic tests.

### API

All libwdog API functions, except `wdog_ping()`, return POSIX OK(0) or
negative value with `errno` set on error.  The `wdog_subscribe()` call
returns a positive integer (including zero) for the watchdog `id`.

```C
/*
 * Enable or disable watchdogd at runtime.
 */
int wdog_enable      (int enable);
int wdog_status      (int *enabled);

/*
 * Check if watchdogd API is actively responding,
 * returns %TRUE(1) or %FALSE(0)
 */
int wdog_ping        (void);

/*
 * Register/unregister with process supervisor
 */
int wdog_subscribe   (char *label, unsigned int timeout, unsigned int *ack);
int wdog_unsubscribe (int id, unsigned int ack);
int wdog_kick        (int id, unsigned int timeout, unsigned int ack, unsigned int *next_ack);
int wdog_kick2       (int id, unsigned int *ack);
int wdog_extend_kick (int id, unsigned int timeout, unsigned int *ack);
```

See [wdog.h](src/wdog.h) or 🕮 [codedocs.xyz](https://codedocs.xyz/troglobit/watchdogd/wdog_8h.html) for detailed API documentation.

It is highly recommended to use an event loop like libev, [libuev][], or
similar.  For such libraries one can simply add a timer callback for the
kick to run periodically to monitor proper operation of the client.


[libuEv]:  https://github.com/troglobit/libuev/
[ex1]:     https://github.com/troglobit/watchdogd/blob/master/examples/ex1.c
