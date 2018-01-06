/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2018  Joachim Nilsson <troglobit@gmail.com>
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
#include "api.h"
#include "conf.h"
#include "script.h"
#include "supervisor.h"

/* Command line options, if set they take precedence over .conf file */
char *opt_config   = NULL;
int   opt_safe     = 0;
char *opt_script   = NULL;
int   opt_timeout  = 0;
int   opt_interval = 0;

/* Global daemon settings */
int magic   = 0;
int enabled = 1;
int loglevel = LOG_NOTICE;
int period = -1;
int timeout = WDT_TIMEOUT_DEFAULT;
int rebooting = 0;
int wait_reboot = 0;
char  *prognm = NULL;

#ifndef TESTMODE_DISABLED
int __wdt_testmode = 0;
#endif

/* Event contexts */
static uev_t period_watcher;
static uev_t sigterm_watcher;
static uev_t sigint_watcher;
static uev_t sigquit_watcher;
static uev_t sigpwr_watcher;
static uev_t sigusr1_watcher;
static uev_t sigusr2_watcher;


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

static void period_cb(uev_t *w, void *arg, int event)
{
	wdt_kick("Kicking watchdog.");
}

static char *progname(char *arg0)
{
	char *nm;

	nm = strrchr(arg0, '/');
	if (nm)
		nm++;
	else
		nm = arg0;
	nm = strdup(nm);

	return nm;
}

static int usage(int status)
{
	printf("Usage:\n"
	       "  %s [-hnsVx] "
#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
	       "[-e CMD] "
#endif
	       "[-f FILE] [-T SEC] [-t SEC] [%s]\n\n"
	       "Example:\n"
	       "  %s -a 0.8,0.9 -T 120 -t 30 /dev/watchdog2\n\n"
               "Options:\n"
#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
               "  -e, --script=CMD         Script or command to run as monitor plugin callback\n"
#endif
	       "  -f, --config=FILE         Use FILE name for configuration\n"
               "  -n, --foreground         Start in foreground, background is default\n"
	       "  -s, --syslog             Use syslog, even if running in foreground\n"
	       "  -l, --loglevel=LVL       Log level: none, err, warn, notice*, info, debug\n"
	       "\n"
               "  -T, --timeout=SEC        HW watchdog timer (WDT) timeout, in SEC seconds\n"
               "  -t, --interval=SEC       WDT kick interval, in SEC seconds, default: %d\n"
               "  -x, --safe-exit          Disable watchdog on exit from SIGINT/SIGTERM,\n"
	       "                           \"magic\" exit may not be supported by HW/driver\n"
	       "  -p, --supervisor[=PRIO]  Enable process supervisor, at elevated RT prio.\n"
	       "                           Default RT prio when active: SCHED_RR @98\n"
	       "\n"
	       "  -V, --version            Display version and exit\n"
               "  -h, --help               Display this help message and exit\n"
	       "\n"
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

#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
#define RUNSCRIPT "e:"
#else
#define RUNSCRIPT
#endif
#define PLUGIN_FLAGS RUNSCRIPT

extern int __wdog_loglevel(char *level);

int main(int argc, char *argv[])
{
	int real_timeout = 0;
	int T;
	int background = 1;
	int use_syslog = 1;
	int c, status, cause;
	int log_opts = LOG_NDELAY | LOG_NOWAIT | LOG_PID;
	char *dev = NULL;
	struct option long_options[] = {
#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
		{"script",        1, 0, 'e'},
#endif
		{"config",        1, 0, 'f'},
		{"foreground",    0, 0, 'n'},
		{"help",          0, 0, 'h'},
		{"interval",      1, 0, 't'},
		{"loglevel",      1, 0, 'l'},
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
	while ((c = getopt_long(argc, argv, PLUGIN_FLAGS "f:Fhl:Lnp::sSt:T:Vx?", long_options, NULL)) != EOF) {
		switch (c) {
#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
		case 'e':
			opt_script = optarg;
			break;
#endif

		case 'f':
			opt_config = optarg;
			break;

		case 'h':
			return usage(0);

		case 'l':
			loglevel = __wdog_loglevel(optarg);
			if (-1 == loglevel)
				return usage(1);
			break;

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
			opt_interval = atoi(optarg);
			break;

		case 'T':	/* Watchdog timeout */
			if (!optarg) {
				ERROR("Missing timeout argument.");
				return usage(1);
			}
			opt_timeout = atoi(optarg);
			break;

		case 'V':
			printf("v%s\n", VERSION);
			return 0;

		case 'x':	/* Safe exit, i.e., don't reboot if we exit and close device */
			opt_safe = 1;
			break;

		default:
			ERROR("Unrecognized option '-%c'.\n", optopt);
			return usage(1);
		}
	}

	/* Create event loop context */
	uev_init(&ctx);

	/* BusyBox watchdogd compat. */
	if (optind < argc)
		dev = argv[optind];

	/* Check for command line options */
	if (!opt_config) {
		/* Default .conf file path: "/etc" + '/' + "watchdogd" + ".conf" */
		size_t len = strlen(SYSCONFDIR) + strlen(PACKAGE) + 7;

		opt_config = malloc(len);
		if (!opt_config) {
			PERROR("Failed allocating memory, exiting");
			return 1;
		}
		snprintf(opt_config, len, "%s/%s.conf", SYSCONFDIR, PACKAGE);
	}
	if (opt_script) {
		if (script_init(&ctx, opt_script))
			return usage(1);
	}
	if (opt_safe)
		magic = 1;
	if (opt_timeout)
		timeout = opt_timeout;
	if (opt_interval)
		period = opt_interval;

	/* Read /etc/watchdogd.conf if it exists */
	if (fexist(opt_config) && conf_parse_file(&ctx, opt_config))
		PERROR("Failed parsing %s", opt_config);

	/* Start daemon */
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

	/* Setup callbacks for SIGUSR1 and, optionally, exit magic on SIGINT/SIGTERM */
	setup_signals(&ctx);

	/*
	 * Mark oursevles a "special" process for Finit/systemd
	 * https://www.freedesktop.org/wiki/Software/systemd/RootStorageDaemons/
	 */
	argv[0][0] = '@';

	if (wdt_init(dev)) {
		PERROR("Failed connecting to kernel watchdog driver");
		return 1;
	}

	/* Read boot cause from watchdog ... */
	cause = wdt_get_bootstatus();

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

	/* Start process supervisor */
	supervisor_init(&ctx, T);

	/* Start client API socket */
	api_init(&ctx);

	/* Create pidfile when we're done with all set up. */
	if (pidfile(prognm) && !wdt_testmode())
		PERROR("Cannot create pidfile");

	status = uev_run(&ctx, 0);
	if (wdt_testmode())
		return status;

	api_exit();
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
