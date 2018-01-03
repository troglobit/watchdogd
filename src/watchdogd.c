/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2016  Joachim Nilsson <troglobit@gmail.com>
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
#include "plugin.h"
#include "rc.h"

#ifndef HAVE_FINIT_FINIT_H
#define check_handover(devnode)						\
	return 1;
#else
#define check_handover(devnode)						\
{									\
	if (EBUSY != errno)						\
		return 1;						\
									\
	/*								\
	 * If we're called in a system with Finit running, tell it to	\
	 * disable its built-in watchdog daemon.			\
	 */								\
	fd = wdt_handover(devnode);					\
	if (fd == -1) {							\
		PERROR("Failed communicating WDT handover with finit");	\
		return 1;						\
	}								\
									\
	wdt_kick("WDT handover complete.");				\
}
#endif

/* Global daemon settings */
int magic   = 0;
int enabled = 1;
int loglevel = LOG_NOTICE;
int wait_reboot = 0;
int period = -1;
int rebooting = 0;
char  *prognm = NULL;

#ifndef TESTMODE_DISABLED
int __wdt_testmode = 0;
#endif

/* Actual reboot reason as read at boot, reported by supervisor API */
wdog_reason_t reboot_reason;

/* Reset cause */
wdog_cause_t reset_cause   = WDOG_SYSTEM_OK;
unsigned int reset_counter = 0;

/* WDT info */
static struct watchdog_info __info;

/* Local variables */
static int fd = -1;
static char devnode[42] = WDT_DEVNODE;

/* Event contexts */
static uev_t period_watcher;
static uev_t sigterm_watcher;
static uev_t sigint_watcher;
static uev_t sigquit_watcher;
static uev_t sigpwr_watcher;
static uev_t timeout_watcher;
static uev_t sigusr1_watcher;
static uev_t sigusr2_watcher;


int wdt_capability(uint32_t flag)
{
	return (__info.options & flag) == flag;
}

/*
 * Connect to kernel wdt driver
 */
int wdt_init(struct watchdog_info *info)
{
	if (wdt_testmode())
		return 0;

	fd = open(devnode, O_WRONLY);
	if (fd == -1)
		check_handover(devnode);

	if (info) {
		memset(info, 0, sizeof(*info));
		ioctl(fd, WDIOC_GETSUPPORT, info);
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
	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("No kick, currently disabled.");
		return 0;
	}

	if (!wdt_capability(WDIOF_CARDRESET))
		INFO("Kicking WDT.");

	return ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

/* FYI: The most common lowest setting is 120 sec. */
int wdt_set_timeout(int count)
{
	int arg = count;

	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("Cannot set timeout, currently disabled.");
		return 0;
	}

	if (!wdt_capability(WDIOF_SETTIMEOUT)) {
		WARN("WDT does not support setting timeout.");
		return 1;
	}

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

	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("Cannot get timeout, currently disabled.");
		return 0;
	}

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

	if (wdt_testmode())
		return status;

	if (fd == -1) {
		DEBUG("Cannot get boot status, currently disabled.");
		return 0;
	}

	if ((err = ioctl(fd, WDIOC_GETBOOTSTATUS, &status)))
		status += err;

	if (!err && status) {
		if (status & WDIOF_POWERUNDER)
			LOG("Reset cause: POWER-ON");
		if (status & WDIOF_FANFAULT)
			LOG("Reset cause: FAN-FAULT");
		if (status & WDIOF_OVERHEAT)
			LOG("Reset cause: CPU-OVERHEAT");
		if (status & WDIOF_CARDRESET)
			LOG("Reset cause: WATCHDOG");
	}

	return status;
}

int wdt_enable(int enable)
{
	int result = 0;

	if (enabled == enable)
		return 0;	/* Hello?  Yes, this is dog */

	if (!enable) {
		/* Attempt to disable HW watchdog */
		if (fd != -1) {
			if (!wdt_capability(WDIOF_MAGICCLOSE)) {
				ERROR("WDT cannot be disabled, aborting operation.");
				return 1;
			}

			INFO("Attempting to disable HW watchdog timer.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now");

			close(fd);
			fd = -1;
		}
	} else {
		result += wdt_init(NULL);
	}

	result += wdt_plugins_enable(enable);
	if (!result)
		enabled = enable;

	return result;
}

int wdt_close(uev_ctx_t *ctx)
{
	/* Let plugins exit before we leave main loop */
	wdt_plugins_exit(ctx);

	if (fd != -1) {
		if (magic) {
			INFO("Disabling HW watchdog timer before (safe) exit.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog before exit, system will likely reboot now");
		} else {
			LOG("Exiting, watchdog still active.  Expect reboot!");
			/* Be nice, sync any buffered data to disk first. */
			sync();
		}

		close(fd);
	}

	/* Leave main loop. */
	return uev_exit(ctx);
}

int wdt_exit(uev_ctx_t *ctx)
{
	/* Let plugins exit before we leave main loop */
	if (!rebooting)
		wdt_plugins_exit(ctx);

	/* Be nice, sync any buffered data to disk first. */
	sync();

	if (fd != -1) {
		DEBUG("Forced watchdog reboot.");
		wdt_set_timeout(1);
		close(fd);
		fd = -1;
	}

	/* Tell main() to loop until reboot ... */
	wait_reboot = 1;

	/* Leave main loop. */
	return uev_exit(ctx);
}

/*
 * Callback for timed reboot
 */
static void reboot_timeout_cb(uev_t *w, void *arg, int events)
{
	wdt_exit(w->ctx);
}

/*
 * Exit and reboot system -- reason for reboot is stored in some form of
 * semi-persistent backend, using @pid and @label, defined at compile
 * time.  By default the backend will be a regular file in /var/lib/,
 * most likely /var/lib/misc/watchdogd.state -- see the FHS for details
 * http://www.pathname.com/fhs/pub/fhs-2.3.html#VARLIBVARIABLESTATEINFORMATION
 */
int wdt_reboot(uev_ctx_t *ctx, pid_t pid, wdog_reason_t *reason, int timeout)
{
	if (!ctx || !reason)
		return errno = EINVAL;

	DEBUG("Reboot requested by pid %d, label %s, timeout: %d ...", pid, reason->label, timeout);

	/* Save reboot cause */
	reason->counter = reset_counter + 1;
	reset_cause_set(reason, pid);

	if (timeout > 0)
		return uev_timer_init(ctx, &timeout_watcher, reboot_timeout_cb, NULL, timeout, 0);

	return wdt_exit(ctx);
}

/* timeout is in milliseconds */
int wdt_forced_reboot(uev_ctx_t *ctx, pid_t pid, char *label, int timeout)
{
	wdog_reason_t reason;

	memset(&reason, 0, sizeof(reason));
	reason.cause = WDOG_FORCED_RESET;
	strlcpy(reason.label, label, sizeof(reason.label));

	return wdt_reboot(ctx, pid, &reason, timeout);
}

static void exit_cb(uev_t *w, void *arg, int events)
{
	DEBUG("Got signal %d, rebooting:%d ...", w->signo, rebooting);
	if (rebooting) {
		wdt_exit(w->ctx);
		return;
	}

	wdt_close(w->ctx);
}

static void reboot_cb(uev_t *w, void *arg, int events)
{
	int timeout = 0;

	DEBUG("Got signal %d, rebooting:%d ...", w->signo, rebooting);
	if (rebooting) {
		wdt_exit(w->ctx);
		return;
	}

	rebooting = 1;

	if (w->signo == SIGPWR)
		timeout = 10000;

	/* XXX: A future version may try to figure out PID of sender */
	wdt_forced_reboot(w->ctx, 1, (char *)arg, timeout);
}

static void ignore_cb(uev_t *w, void *arg, int events)
{
	DEBUG("Ignoring SIG%s", (char *)arg);
}

static void setup_signals(uev_ctx_t *ctx)
{
	/* Signals to stop watchdogd */
#ifndef COMPAT_SUPERVISOR
	uev_signal_init(ctx, &sigterm_watcher, exit_cb, NULL, SIGTERM);
	uev_signal_init(ctx, &sigquit_watcher, exit_cb, NULL, SIGQUIT);
#else
	uev_signal_init(ctx, &sigterm_watcher, reboot_cb, "*REBOOT*", SIGTERM);
	uev_signal_init(ctx, &sigquit_watcher, reboot_cb, "client", SIGQUIT);
#endif
	uev_signal_init(ctx, &sigint_watcher,  exit_cb, NULL, SIGINT);

	/* Watchdog reboot support */
	uev_signal_init(ctx, &sigpwr_watcher, reboot_cb, "init", SIGPWR);

	/* Ignore signals older watchdogd used, in case of older userland */
	uev_signal_init(ctx, &sigusr1_watcher, ignore_cb, "USR1", SIGUSR1);
	uev_signal_init(ctx, &sigusr2_watcher, ignore_cb, "USR2", SIGUSR2);
}

int wdt_fload_reason(FILE *fp, wdog_reason_t *r, pid_t *pid)
{
	char *ptr, buf[80];
	pid_t dummy;

	if (!pid)
		pid = &dummy;

	while ((ptr = fgets(buf, sizeof(buf), fp))) {
		if (sscanf(buf, WDT_REASON_CNT ": %u\n", &r->counter) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_PID ": %d\n", pid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_WID ": %d\n", &r->wid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_CSE ": %d\n", (int *)&r->cause) == 1)
			continue;

		if (string_match(buf, WDT_REASON_LBL ": ")) {
			ptr += strlen(WDT_REASON_LBL) + 2;
			strlcpy(r->label, chomp(ptr), sizeof(r->label));
			continue;
		}
	}

	return fclose(fp);
}

int wdt_fstore_reason(FILE *fp, wdog_reason_t *r, pid_t pid)
{
	fprintf(fp, WDT_REASON_CNT ": %u\n", r->counter);
	fprintf(fp, WDT_REASON_PID ": %d\n", pid);
	fprintf(fp, WDT_REASON_WID ": %d\n", r->wid);
	fprintf(fp, WDT_REASON_LBL ": %s\n", r->label);
	fprintf(fp, WDT_REASON_CSE ": %d\n", r->cause);
	fprintf(fp, WDT_REASON_STR ": %s\n", wdog_reboot_reason_str(r));

	return fclose(fp);
}

static int load_bootstatus(char *file, wdog_reason_t *r, pid_t *pid)
{
	FILE *fp;

	fp = fopen(file, "r");
	if (!fp)
		return -1;

	return wdt_fload_reason(fp, r, pid);
}

#ifdef COMPAT_SUPERVISOR
static int compat_supervisor(wdog_reason_t *r)
{
	FILE *fp;

	/* Compat, created at boot from RTC contents */
	fp = fopen(_PATH_VARRUN "supervisor.status", "w");
        if (!fp) {
		PERROR("Failed creating compat boot status");
		return -1;
	}

	fprintf(fp, "Watchdog ID  : %d\n", r->wid);
	fprintf(fp, "Label        : %s\n", r->label);
	fprintf(fp, "Reset cause  : %d (%s)\n", r->cause, wdog_get_reason_str(r));
	fprintf(fp, "Counter      : %u\n", r->counter);

	return fclose(fp);
}
#else
#define compat_supervisor(r) 0
#endif /* COMPAT_SUPERVISOR */

static int create_bootstatus(char *fn, wdog_reason_t *r, int cause, int timeout, int interval, pid_t pid)
{
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		PERROR("Failed opening %s", WDOG_STATUS);
		return -1;
	}

	fprintf(fp, WDT_REASON_WDT ": 0x%04x\n", cause >= 0 ? cause : 0);
	fprintf(fp, WDT_REASON_TMO ": %d\n", timeout);
	fprintf(fp, WDT_REASON_INT ": %d\n", interval);

	 return wdt_fstore_reason(fp, r, pid);
}

static int wdt_set_bootstatus(int cause, int timeout, int interval)
{
	pid_t pid = 0;
	char *status;
	wdog_reason_t reason;

	if (wdt_testmode())
		status = WDOG_STATUS_TEST;
	else
		status = WDOG_STATUS;

	/*
	 * In case we're restarted at runtime this prevents us from
	 * recreating the status file(s).
	 */
	if (fexist(status)) {
		load_bootstatus(status, &reboot_reason, &pid);
		reset_cause   = reboot_reason.cause;
		reset_counter = reboot_reason.counter;

		return create_bootstatus(status, &reboot_reason, cause, timeout, interval, pid);
	}

	memset(&reason, 0, sizeof(reason));
	if (!reset_cause_get(&reason, &pid)) {
		reset_cause   = reason.cause;
		reset_counter = reason.counter;
	}

	/*
	 * Clear latest reset cause log IF and only IF WDT reports power
	 * failure as cause of this boot.  Keep reset counter, that must
	 * be reset using the API, snmpEngineBoots (RFC 2574)
	 */
	if (cause & WDIOF_POWERUNDER) {
		memset(&reason, 0, sizeof(reason));
		reason.counter = reset_counter;
		pid = 0;
	}

	if (!create_bootstatus(status, &reason, cause, timeout, interval, pid))
		memcpy(&reboot_reason, &reason, sizeof(reboot_reason));

	/*
	 * Prepare for power-loss or otherwise uncontrolled reset
	 * The operator expects us to track the n:o restarts ...
	 */
	memset(&reason, 0, sizeof(reason));
	reason.cause   = WDOG_FAILED_UNKNOWN;
	reason.counter = reset_counter + 1;
	reset_cause_clear(&reason);

	if (wdt_testmode())
		return 0;

	return compat_supervisor(&reboot_reason);
}

static void period_cb(uev_t *w, void *arg, int event)
{
	wdt_kick("Kicking watchdog.");
}

/*
 * Mark oursevles a "special" process for Finit/systemd
 * https://www.freedesktop.org/wiki/Software/systemd/RootStorageDaemons/
 */
static char *progname(char *arg0)
{
	int i;
	char *nm;

	nm = strrchr(arg0, '/');
	if (nm)
		nm++;
	else
		nm = arg0;
	nm = strdup(nm);

	for (i = 0; arg0[i]; i++)
		arg0[i] = 0;
	sprintf(arg0, "@%s", PACKAGE);

	return nm;
}

static int usage(int status)
{
	printf("Usage:\n"
	       "  %s [-hnsVx] "
#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
	       "[-e CMD] "
#endif
#ifdef LOADAVG_PERIOD
	       "[-a W,R] "
#endif
#ifdef MEMINFO_PERIOD
	       "[-m W,R] "
#endif
#ifdef FILENR_PERIOD
	       "[-f W,R] "
#endif
	       "[-T SEC] [-t SEC] [%s]\n\n"
	       "Example:\n"
	       "  %s -a 0.8,0.9 -T 120 -t 30 /dev/watchdog2\n\n"
               "Options:\n"
#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
               "  -e, --script=CMD         Script or command to run as monitor plugin callback\n"
#endif
               "  -n, --foreground         Start in foreground, background is default\n"
	       "  -s, --syslog             Use syslog, even if running in foreground\n"
	       "  -l, --loglevel=LVL       Log level: none, err, warn, notice*, info, debug\n"
	       "\n"
               "  -T, --timeout=SEC        HW watchdog timer (WDT) timeout, in SEC seconds\n"
               "  -t, --interval=SEC       WDT kick interval, in SEC seconds, default: %d\n"
               "  -x, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM,\n"
	       "                           \"magic\" exit may not be supported by HW/driver\n"
#ifdef LOADAVG_PERIOD
	       "\n"
	       "  -a, --load-average=W,R   Enable load average check, WARN,REBOOT\n"
#endif
#ifdef MEMINFO_PERIOD
	       "  -m, --meminfo=W,R        Enable memory leak check, WARN,REBOOT\n"
#endif
#ifdef FILENR_PERIOD
	       "  -f, --filenr=W,R         Enable file descriptor leak check, WARN,REBOOT\n"
#endif
	       "  -p, --supervisor[=PRIO]  Enable process supervisor, at elevated RT prio.\n"
	       "                           Default RT prio when active: SCHED_RR @98\n"
	       "\n"
	       "  -V, --version            Display version and exit\n"
               "  -h, --help               Display this help message and exit\n"
	       "\n"
#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
	       "WARN,REBOOT ranges are 0-1 (0-100%%), except for load average which can vary\n"
	       "a lot between systems and use-cases, not just because of the number of CPU\n"
	       "cores.  Use `-e CMD` to call script on WARN and REBOOT, instead of performing\n"
	       "an unconditional reboot on REBOOT.\n"
	       "Note: the REBOOT argument is optional, omitting it disables reboot action.\n"
	       "\n"
#endif
	       "Bug report address: %s\n", prognm, WDT_DEVNODE, prognm, WDT_KICK_DEFAULT,
	       PACKAGE_BUGREPORT);

	return status;
}

int wdt_debug(int enable)
{
	static int oldlevel = 0;

	if (enable) {
		if (!oldlevel)
			oldlevel = loglevel;
		loglevel = LOG_DEBUG;
	} else {
		if (oldlevel) {
			loglevel = oldlevel;
			oldlevel = 0;
		}
	}

	setlogmask(LOG_UPTO(loglevel));

	return 0;
}

#ifdef LOADAVG_PERIOD
#define LOADAVG "a:"
#else
#define LOADAVG ""
#endif
#ifdef FILENR_PERIOD
#define FILENR "f:"
#else
#define FILENR ""
#endif
#ifdef MEMINFO_PERIOD
#define MEMINFO "m:"
#else
#define MEMINFO ""
#endif
#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
#define RUNSCRIPT "e:"
#else
#define RUNSCRIPT
#endif
#define PLUGIN_FLAGS RUNSCRIPT LOADAVG FILENR MEMINFO

extern int __wdog_loglevel(char *level);

int main(int argc, char *argv[])
{
	int timeout = WDT_TIMEOUT_DEFAULT;
	int real_timeout = 0;
	int T;
	int background = 1;
	int use_syslog = 1;
	int c, status, cause;
	int log_opts = LOG_NDELAY | LOG_NOWAIT | LOG_PID;
	struct option long_options[] = {
#ifdef LOADAVG_PERIOD
		{"load-average",  1, 0, 'a'},
#endif
#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
		{"script",        1, 0, 'e'},
#endif
		{"foreground",    0, 0, 'n'},
		{"help",          0, 0, 'h'},
		{"interval",      1, 0, 't'},
		{"loglevel",      1, 0, 'l'},
#ifdef MEMINFO_PERIOD
		{"meminfo",       1, 0, 'm'},
#endif
#ifdef FILENR_PERIOD
		{"filenr",        1, 0, 'f'},
#endif
		{"safe-exit",     0, 0, 'x'},
		{"supervisor",    2, 0, 'p'},
		{"syslog",        0, 0, 's'},
#ifndef TESTMODE_DISABLED
		{"test-mode",     0, 0, 'S'}, /* Hidden test mode, not for public use. */
#endif
		{"version",       0, 0, 'V'},
		{"timeout",       1, 0, 'T'},
		{NULL, 0, 0, 0}
	};
	uev_ctx_t ctx;

	prognm = progname(argv[0]);
	while ((c = getopt_long(argc, argv, PLUGIN_FLAGS "Fhl:Lnp::sSt:T:Vx?", long_options, NULL)) != EOF) {
		switch (c) {
		case 'a':
#ifdef LOADAVG_PERIOD
			if (loadavg_set(optarg))
			    return usage(1);
			break;
#endif

#if defined(LOADAVG_PERIOD) || defined(MEMINFO_PERIOD) || defined(FILENR_PERIOD)
		case 'e':
			if (script_init(optarg))
				return usage(1);
			break;
#endif

#ifdef FILENR_PERIOD
		case 'f':
			if (filenr_set(optarg))
				return usage(1);
			break;
#endif

		case 'h':
			return usage(0);

		case 'l':
			loglevel = __wdog_loglevel(optarg);
			if (-1 == loglevel)
				return usage(1);
			break;

#ifdef MEMINFO_PERIOD
		case 'm':
			if (meminfo_set(optarg))
				return usage(1);
			break;
#endif

		case 'F':	/* BusyBox watchdogd compat. */
		case 'n':	/* Run in foreground */
			background = 0;
			use_syslog--;
			break;

		case 'p':
			if (supervisor_set(optarg))
				return usage(1);
			break;

		case 's':
			use_syslog++;
			break;

#ifndef TESTMODE_DISABLED
		case 'S':	/* Simulate: no interaction with kernel, for testing supervisor */
			__wdt_testmode = 1;
			break;
#endif

		case 't':	/* Watchdog kick interval */
			if (!optarg) {
				ERROR("Missing interval argument.");
				return usage(1);
			}
			period = atoi(optarg);
			break;

		case 'T':	/* Watchdog timeout */
			if (!optarg) {
				ERROR("Missing timeout argument.");
				return usage(1);
			}
			timeout = atoi(optarg);
			break;

		case 'V':
			printf("v%s\n", VERSION);
			return 0;

		case 'x':	/* Safe exit, i.e., don't reboot if we exit and close device */
			magic = 1;
			break;

		default:
			ERROR("Unrecognized option '-%c'.\n", optopt);
			return usage(1);
		}
	}

	/* BusyBox watchdogd compat. */
	if (optind < argc) {
		char *dev = argv[optind];

		if (!strncmp(dev, "/dev", 4))
			strlcpy(devnode, dev, sizeof(devnode));
	}

	if (background) {
		DEBUG("Daemonizing ...");

		if (-1 == daemon(0, 0)) {
			PERROR("Failed daemonizing");
			return 1;
		}
	}

	if (!background && use_syslog < 1)
		log_opts |= LOG_PERROR;

	setlogmask(LOG_UPTO(loglevel));
	openlog(prognm, log_opts, LOG_DAEMON);

	LOG("%s v%s %s ...", prognm, PACKAGE_VERSION, wdt_testmode() ? "test mode" : "starting");
	uev_init(&ctx);

	/* Setup callbacks for SIGUSR1 and, optionally, exit magic on SIGINT/SIGTERM */
	setup_signals(&ctx);

	if (wdt_init(&__info)) {
		PERROR("Failed connecting to kernel watchdog driver");
		return 1;
	}

	/* Read boot cause from watchdog ... */
	cause = wdt_get_bootstatus();
	INFO("%s: %s, capabilities 0x%04x", devnode, __info.identity, __info.options);

	/* Check capabilities */
	if (magic && !wdt_capability(WDIOF_MAGICCLOSE)) {
		WARN("WDT cannot be disabled, disabling safe exit.");
		magic = 0;
	}

	if (!wdt_capability(WDIOF_POWERUNDER))
		WARN("WDT does not support PWR fail condition, treating as card reset.");

	/* Set requested WDT timeout right before we enter the event loop. */
	if (wdt_set_timeout(timeout))
		PERROR("Failed setting HW watchdog timeout: %d", timeout);

	/* Sanity check with driver that setting actually took. */
	real_timeout = wdt_get_timeout();
	if (real_timeout < 0) {
		real_timeout = WDT_TIMEOUT_DEFAULT;
		PERROR("Failed reading current watchdog timeout");
	} else {
		if (real_timeout <= period) {
			ERROR("Warning, watchdog timeout <= kick interval: %d <= %d",
			      real_timeout, period);
		}
	}

	/* If user did not provide '-t' interval, set to half WDT timeout */
	if (-1 == period) {
		period = real_timeout / 2;
		if (!period)
			period = 1;
	}

	/* ... save boot cause in /var/run/watchdogd.status */
	wdt_set_bootstatus(cause, real_timeout, period);

	/* Calculate period (T) in milliseconds for libuEv */
	T = period * 1000;
	DEBUG("Watchdog kick interval set to %d sec.", period);

	/* Every period (T) seconds we kick the WDT */
	uev_timer_init(&ctx, &period_watcher, period_cb, NULL, T, T);

	/* Start all enabled plugins */
	wdt_plugins_init(&ctx, T);

	/* Create pidfile when we're done with all set up. */
	if (pidfile(prognm) && !wdt_testmode())
		PERROR("Cannot create pidfile");

	status = uev_run(&ctx, 0);
	if (wdt_testmode())
		return status;

	while (wait_reboot) {
		int reboot_in = 3 * real_timeout;

		INFO("Waiting for HW WDT reboot ...");
		while (reboot_in > 0) {
			unsigned int rest = sleep(real_timeout);

			while (rest)
				rest = sleep(rest);

			reboot_in -= real_timeout;
		}

		LOG("HW WDT did not reboot, forcing reboot now ...");
		reboot(RB_AUTOBOOT);
	}

	return status;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
