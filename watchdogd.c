/* A small userspace watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2015  Joachim Nilsson <troglobit@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wdt.h"
#include "filenr.h"
#include "loadavg.h"
#include "meminfo.h"
#include "pmon.h"

/* Global daemon settings */
int magic   = 0;
int enabled = 1;
int verbose = 0;
int sys_log = 0;
int extkick = 0;
int extdelay = 0;
int wait_reboot = 0;
int period = -1;
int testmode = 0;

/* Local variables */
static int fd = -1;
static char devnode[42] = WDT_DEVNODE;


/* Event contexts */
static uev_t period_watcher;
static uev_t sigterm_watcher;
static uev_t sigint_watcher;
static uev_t sigquit_watcher;
static uev_t sigpwr_watcher;
static uev_t sigusr1_watcher;
static uev_t sigusr2_watcher;


/*
 *
 */
static void plugins_init(uev_ctx_t *ctx, int T)
{
	/* Start file descriptor monitor */
	filenr_init(ctx, T);

	/* Start load average monitor, if enabled */
	loadavg_init(ctx, T);

	/* Start memory leak monitor */
	meminfo_init(ctx, T);

	/* Start process monitor */
	pmon_init(ctx, T);
}

/*
 *
 */
static void plugins_exit(uev_ctx_t *ctx)
{
	pmon_exit(ctx);
}

/*
 * Connect to kernel wdt driver
 */
int wdt_init(void)
{
	if (testmode)
		return 0;

	fd = open(devnode, O_WRONLY);
	if (fd == -1) {
		DEBUG("Failed opening watchdog device %s: %s", devnode, strerror(errno));
		return 1;
	}

	return 0;
}

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
int wdt_kick(char *msg)
{
	int dummy;

	DEBUG("%s", msg);
	if (testmode)
		return 0;

	return ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

/* FYI: The most common lowest setting is 120 sec. */
int wdt_set_timeout(int count)
{
	int arg = count;

	if (testmode)
		return 0;

	DEBUG("Setting watchdog timeout to %d sec.", count);
	if (ioctl(fd, WDIOC_SETTIMEOUT, &arg))
		return 1;

	DEBUG("Previous timeout was %d sec", arg);

	return 0;
}

int wdt_get_timeout(void)
{
	int count;
	int err;

	if (testmode)
		return 0;

	err = ioctl(fd, WDIOC_GETTIMEOUT, &count);
	if (err)
		count = err;

	DEBUG("Watchdog timeout is set to %d sec.", count);

	return count;
}

int wdt_get_bootstatus(void)
{
	int status = 0;
	int err;

	if (testmode)
		return status;

	if ((err = ioctl(fd, WDIOC_GETBOOTSTATUS, &status)))
		status = err;

	if (!err && status) {
		if (status & WDIOF_POWERUNDER)
			INFO("Reset cause: POWER-ON");
		if (status & WDIOF_FANFAULT)
			INFO("Reset cause: FAN-FAULT");
		if (status & WDIOF_OVERHEAT)
			INFO("Reset cause: CPU-OVERHEAT");
		if (status & WDIOF_CARDRESET)
			INFO("Reset cause: WATCHDOG");
	}

	return status;
}

int wdt_enable(int enable)
{
	int result = 0;

	if (!enable) {
		/* Attempt to disable HW watchdog */
		if (fd != 1) {
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now");
			close(fd);
			fd = -1;
		}
	} else {
		result += wdt_init();
	}

	result += pmon_enable(enable);

	return result;
}

int wdt_close(uev_ctx_t *ctx)
{
	/* Let plugins exit before we leave main loop */
	plugins_exit(ctx);

	if (fd != -1) {
		if (magic) {
			INFO("Disabling HW watchdog timer before (safe) exit.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now");
		} else {
			INFO("Exiting, watchdog still active.  Expect reboot!");
			/* Be nice, sync any buffered data to disk first. */
			sync();
		}

		close(fd);
	}

	/* Leave main loop. */
	return uev_exit(ctx);
}

void exit_cb(uev_t *w, void *UNUSED(arg), int UNUSED(events))
{
	wdt_close(w->ctx);
}

int wdt_reboot(uev_ctx_t *ctx)
{
	/* Let plugins exit before we leave main loop */
	plugins_exit(ctx);

	/* Be nice, sync any buffered data to disk first. */
	sync();

	if (fd != -1) {
		INFO("Forced watchdog reboot.");
		wdt_set_timeout(1);
		close(fd);
	}

	/* Tell main() to loop until reboot ... */
	wait_reboot = 1;

	/* Leave main loop. */
	return uev_exit(ctx);
}

void reboot_cb(uev_t *w, void *UNUSED(arg), int UNUSED(events))
{
	wdt_reboot(w->ctx);
}

static void ext_kick_cb(uev_t *UNUSED(w), void *UNUSED(arg), int UNUSED(events))
{
	if (!extkick) {
		extdelay = 0;
		extkick = 1;
		INFO("External supervisor now controls watchdog kick via SIGUSR1.");
	}

	wdt_kick("External kick.");
}

static void ext_kick_exit_cb(uev_t *UNUSED(w), void *UNUSED(arg), int UNUSED(events))
{
	INFO("External supervisor requested safe exit.  Reverting to built-in kick.");
	extkick = 0;
}

static void setup_signals(uev_ctx_t *ctx)
{
	/* Signals to stop watchdogd */
	uev_signal_init(ctx, &sigterm_watcher, exit_cb, NULL, SIGTERM);
	uev_signal_init(ctx, &sigint_watcher,  exit_cb, NULL, SIGINT);
	uev_signal_init(ctx, &sigquit_watcher, exit_cb, NULL, SIGQUIT);

	/* Watchdog reboot support */
	uev_signal_init(ctx, &sigpwr_watcher, reboot_cb, NULL, SIGPWR);

	/* Kick from external process supervisor */
	uev_signal_init(ctx, &sigusr1_watcher, ext_kick_cb, NULL, SIGUSR1);

	/* Handle graceful exit by external supervisor */
	uev_signal_init(ctx, &sigusr2_watcher, ext_kick_exit_cb, NULL, SIGUSR2);
}

static int create_bootstatus(int timeout, int interval)
{
	int err, cause = 0;
	char *status;

	err = asprintf(&status, "%s%s.status", _PATH_VARRUN, __progname);
	if (-1 != err && status) {
		FILE *fp;

		fp = fopen(status, "w");
		if (fp) {
			int cause = wdt_get_bootstatus();

			fprintf(fp, "Reset cause (WDIOF) : 0x%04x\n", cause >= 0 ? cause : 0);
			fprintf(fp, "Timeout (sec)       : %d\n", timeout);
			fprintf(fp, "Kick interval       : %d\n", interval);

			fclose(fp);
		}

		free(status);
	}

	return cause;
}

static void period_cb(uev_t *UNUSED(w), void *UNUSED(arg), int UNUSED(event))
{
	/* When an external supervisor once has started sending SIGUSR1
	 * it fully assumes responsibility for kicking. No magic here. */
	if (!extkick)
		wdt_kick("Kicking watchdog.");

	/* Startup delay before handing over to external kick.
	 * Wait MAX:@extdelay number of built-in kicks, MIN:1 */
	if (extdelay) {
		DEBUG("Pending external kick in %d sec ...", extdelay * period);
		if (!--extdelay)
			extkick = 1;
	}
}

/*
 * Parse and set monitor plugin warning and critical level arg.
 */
int wdt_set_plugin_arg(char *desc, char *arg, double *warning, double *critical)
{
	char buf[16], *ptr;
	double value;

	if (!arg) {
		ERROR("%s argument missing.", desc);
		return 1;
	}

	strlcpy(buf, arg, sizeof(buf));
	ptr = strchr(buf, ',');
	if (ptr) {
		*ptr++ = 0;
		DEBUG("Found second arg: %s", ptr);
	}

	/* First argument is warning */
	value = strtod(buf, NULL);
	if (value <= 0) {
	error:
		ERROR("%s argument invalid or too small.", desc);
		return 1;
	}
	*warning = value;

	/* Second argument, if given, is warning */
	if (ptr) {
		value = strtod(ptr, NULL);
		if (value <= 0)
			goto error;
	} else {
		/* Backwards compat, when only warning is given */
		value += 0.1;
	}
	*critical = value;

	DEBUG("%s monitor: %.2f, %.2f", desc, *warning, *critical);

	return 0;
}

static int usage(int status)
{
	printf("Usage:\n"
	       "  %s [-fxLsVvh] [-d /dev/watchdog] [-a WARN,REBOOT] [-w SEC] [-k SEC]\n\n"
	       "Example:\n"
	       "  %s -d /dev/watchdog2 -a 0.8,0.9 -w 120 -k 30\n\n"
               "Options:\n"
	       "  -d, --device=<dev>       Device to use, default: %s\n"
               "  -f, --foreground         Start in foreground (background is default)\n"
	       "  -x, --external-kick[=N]  Force external watchdog kick using SIGUSR1\n"
	       "                           A 'N x <interval>' delay for startup is given\n"
	       "  -l, --logfile=<file>     Log to <file> in background, otherwise silent\n"
	       "  -L, --syslog             Use syslog, even if in foreground\n"
               "  -w, --timeout=<sec>      Set the HW watchdog timeout to <sec> seconds\n"
               "  -k, --interval=<sec>     Set watchdog kick interval to <sec> seconds\n"
               "  -s, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM\n"
	       "\n"
	       "  -a, --load-average=<val> Enable load average check, <WARN,REBOOT>\n"
	       "  -m, --meminfo=<val>      Enable memory leak check, <WARN,REBOOT>\n"
	       "  -n, --filenr=<val>       Enable file descriptor leak check, <WARN,REBOOT>\n"
	       "  -p, --pmon[=PRIO]        Enable process monitor, run at elevated RT prio.\n"
	       "                           Default RT prio when active: SCHED_RR @98\n"
	       "\n"
	       "  -V, --verbose            Verbose, noisy output suitable for debugging\n"
	       "  -v, --version            Display version and exit\n"
               "  -h, --help               Display this help message and exit\n"
	       "\n"
               "Kicks %s every %d sec, loadavg monitor disabled, by default\n"
	       "\n"
	       "Most WDT drivers only support 120 sec as lowest timeout, but %s\n"
	       "tries to set %d sec timeout.  Example values above are recommendations\n"
	       "\n",
               __progname, __progname,
	       WDT_DEVNODE, WDT_DEVNODE, WDT_TIMEOUT_DEFAULT / 2,
	       __progname, WDT_TIMEOUT_DEFAULT);

	return status;
}

int main(int argc, char *argv[])
{
	int timeout = WDT_TIMEOUT_DEFAULT;
	int real_timeout = 0;
	int T;
	int background = 1;
	int c, status;
	char *logfile = NULL;
	struct option long_options[] = {
		{"load-average",  1, 0, 'a'},
		{"device",        1, 0, 'd'},
		{"foreground",    0, 0, 'f'},
		{"help",          0, 0, 'h'},
		{"interval",      1, 0, 'k'},
		{"logfile",       1, 0, 'l'},
		{"syslog",        0, 0, 'L'},
		{"meminfo",       1, 0, 'm'},
		{"filenr",        1, 0, 'n'},
		{"pmon",          2, 0, 'p'},
		{"safe-exit",     0, 0, 's'},
		{"test-mode",     0, 0, 't'},
		{"verbose",       0, 0, 'V'},
		{"version",       0, 0, 'v'},
		{"timeout",       1, 0, 'w'},
		{"external-kick", 2, 0, 'x'},
		{NULL, 0, 0, 0}
	};
	uev_ctx_t ctx;

	while ((c = getopt_long(argc, argv, "a:d:fhl:Lm:n:w:k:p::stVvx::?", long_options, NULL)) != EOF) {
		switch (c) {
		case 'a':
			if (loadavg_set(optarg))
			    return usage(1);
			break;

		case 'd':
			strlcpy(devnode, optarg, sizeof(devnode));
			break;

		case 'f':	/* Run in foreground */
			background = 0;
			break;

		case 'h':
			return usage(0);

		case 'k':	/* Watchdog kick interval */
			if (!optarg) {
				ERROR("Missing interval argument.");
				return usage(1);
			}
			period = atoi(optarg);
			break;

		case 'l':	/* Log to file */
			if (!optarg) {
				ERROR("Missing logfile argument.");
				return usage(1);
			}
			logfile = strdup(optarg);
			break;

		case 'L':	/* Force use of syslog, regardless */
			sys_log = 1;
			break;

		case 'm':
			if (meminfo_set(optarg))
				return usage(1);
			break;

		case 'n':
			if (filenr_set(optarg))
				return usage(1);
			break;

		case 'p':
			if (pmon_set(optarg))
				return usage(1);
			break;

		case 's':	/* Safe exit, i.e., don't reboot if we exit and close device */
			magic = 1;
			break;

		case 't':	/* Enable test mode, no interaction with kernel, for testing pmon */
			testmode = 1;
			break;

		case 'v':
			printf("v%s\n", VERSION);
			return 0;

		case 'V':
			verbose = 1;
			break;

		case 'w':	/* Watchdog timeout */
			if (!optarg) {
				ERROR("Missing timeout argument.");
				return usage(1);
			}
			timeout = atoi(optarg);
			break;

		case 'x':
			if (!optarg)
				extdelay = 1; /* Default is 1 x period */
			else
				extdelay = atoi(optarg);
			break;

		default:
			printf("Unrecognized option \"-%c\".\n", c);
			return usage(1);
		}
	}

	if (background) {
		DEBUG("Daemonizing ...");

		/* If backgrounding and no logfile is given, use syslog */
		if (!logfile)
			sys_log = 1;

		if (-1 == daemon(0, 0)) {
			PERROR("Failed daemonizing");
			return 1;
		}
	}

	if (sys_log)
		openlog(__progname, LOG_NDELAY | LOG_NOWAIT | LOG_PID, LOG_DAEMON);

	INFO("Userspace watchdog daemon v%s starting ...", VERSION);
	uev_init(&ctx);

	/* Setup callbacks for SIGUSR1 and, optionally, exit magic on SIGINT/SIGTERM */
	setup_signals(&ctx);

	if (wdt_init())
		errx(1, "Failed connecting to kernel watchdog driver, exiting!");

	/* Set requested WDT timeout right before we enter the event loop. */
	if (wdt_set_timeout(timeout))
		PERROR("Failed setting HW watchdog timeout: %d", timeout);

	/* Sanity check with driver that setting actually took. */
	real_timeout = wdt_get_timeout();
	if (real_timeout < 0) {
		PERROR("Failed reading current watchdog timeout");
	} else {
		if (real_timeout <= period) {
			ERROR("Warning, watchdog timeout <= kick interval: %d <= %d",
			      real_timeout, period);
		}
	}

	/* If user did not provide '-k' argument, set to half actual timeout */
	if (-1 == period) {
		if (real_timeout < 0)
			period = WDT_KICK_DEFAULT;
		else
			period = real_timeout / 2;

		if (!period)
			period = 1;
	}

	/* Calculate period (T) in milliseconds for libuEv */
	T = period * 1000;
	DEBUG("Watchdog kick interval set to %d sec.", period);

	/* Read boot cause from watchdog and save in /var/run/watchdogd.status */
	create_bootstatus(real_timeout, period);

	/* Every period (T) seconds we kick the wdt */
	uev_timer_init(&ctx, &period_watcher, period_cb, NULL, T, T);

	/* Start all enabled plugins */
	plugins_init(&ctx, T);

	/* Only create pidfile when we're done with all set up. */
	if (pidfile(NULL))
		PERROR("Cannot create pidfile");

	status = uev_run(&ctx, 0);

	while (!testmode && wait_reboot) {
		INFO("Waiting for HW WDT reboot ...");
		sleep(period);
	}

	return status;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
