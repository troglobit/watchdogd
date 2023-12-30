/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2023  Joachim Wiberg <troglobit@gmail.com>
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
static uev_t sigterm_watcher;
static uev_t sigint_watcher;
static uev_t sigquit_watcher;
static uev_t sighup_watcher;
static uev_t sigpwr_watcher;
static uev_t sigusr1_watcher;
static uev_t sigusr2_watcher;

static void pidfile_touch(void)
{
	if (pidfile(WDOG_PIDFILE) && !wdt_testmode())
		PERROR("Cannot create pidfile");
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
	int delay = 0;

	DEBUG("Got signal %d, rebooting:%d ...", w->signo, rebooting);
	if (rebooting) {
		wdt_exit(w->ctx);
		return;
	}

	rebooting = 1;

	if (w->signo == SIGPWR)
		delay = 10000;

	/* XXX: A future version may try to figure out PID of sender */
	wdt_forced_reset(w->ctx, 1, (char *)arg, delay);
}

static void reload_cb(uev_t *w, void *arg, int events)
{
	INFO("SIGHUP received, reloading %s", opt_config ?: "nothing");
	if (conf_parse_file(w->ctx, opt_config))
		return;

	wdt_init(w->ctx, NULL);

	/* Touch PID file to tell Finit we're done with HUP */
	pidfile_touch();
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

	/* Reload .conf file */
	uev_signal_init(ctx, &sighup_watcher, reload_cb, NULL, SIGHUP);

	/* Watchdog reboot support */
	uev_signal_init(ctx, &sigpwr_watcher, reboot_cb, "init", SIGPWR);

	/* Ignore signals older watchdogd used, in case of older userland */
	uev_signal_init(ctx, &sigusr1_watcher, ignore_cb, "USR1", SIGUSR1);
	uev_signal_init(ctx, &sigusr2_watcher, ignore_cb, "USR2", SIGUSR2);
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
	       "  %s [-hnsVx] [-f FILE] [-T SEC] [-t SEC] [%s]\n\n"
	       "Example:\n"
	       "  %s -T 120 -t 30 /dev/watchdog2\n\n"
               "Options:\n"
	       "  -f, --config=FILE   Use FILE for daemon configuration\n"
               "  -n, --foreground    Start in foreground, background is default\n"
	       "  -s, --syslog        Use syslog, even if running in foreground\n"
	       "  -l, --loglevel=LVL  Log level: none, err, warn, notice*, info, debug\n"
	       "\n"
               "  -T, --timeout=SEC   Watchdog timer (WDT) timeout, in seconds, default: %d\n"
               "  -t, --interval=SEC  WDT kick interval, in seconds, default: %d\n"
               "  -x, --safe-exit     Disable watchdog on exit from SIGINT/SIGTERM,\n"
	       "                      \"magic\" exit may not be supported by HW/driver\n"
	       "\n"
	       "  -V, --version       Display version and exit\n"
               "  -h, --help          Display this help message and exit\n"
	       "\n"
	       "Bug report address: %s\n", prognm, WDT_DEVNODE, prognm,
	       WDT_TIMEOUT_DEFAULT, WDT_KICK_DEFAULT, PACKAGE_BUGREPORT);

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

extern int __wdog_loglevel(char *level);

int main(int argc, char *argv[])
{
	int background = 1;
	int use_syslog = 1;
	int c, status;
	int log_opts = LOG_NDELAY | LOG_NOWAIT | LOG_PID;
	char *dev = NULL;
	struct option long_options[] = {
		{"config",        1, 0, 'f'},
		{"foreground",    0, 0, 'n'},
		{"help",          0, 0, 'h'},
		{"interval",      1, 0, 't'},
		{"loglevel",      1, 0, 'l'},
		{"safe-exit",     0, 0, 'x'},
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
	while ((c = getopt_long(argc, argv, "f:Fhl:LnsSt:T:Vx?", long_options, NULL)) != EOF) {
		switch (c) {
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
	if (opt_safe)
		magic = 1;
	if (opt_timeout)
		timeout = opt_timeout;
	if (opt_interval)
		period = opt_interval;

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

	/* Hello world ... */
	LOG("%s v%s %s ...", prognm, PACKAGE_VERSION, wdt_testmode() ? "test mode" : "starting");
	if (wdt_testmode())
		mkpath(WDOG_TESTDIR, 0755);
	else
		mkpath(WDOG_STATUSDIR, 0755);

	/* Read /etc/watchdogd.conf if it exists */
	conf_parse_file(&ctx, opt_config);

	/* Setup callbacks for SIGUSR1 and, optionally, exit magic on SIGINT/SIGTERM */
	setup_signals(&ctx);

	/*
	 * Mark oursevles a "special" process for Finit/systemd
	 * https://www.freedesktop.org/wiki/Software/systemd/RootStorageDaemons/
	 */
	argv[0][0] = '@';

	if (wdt_init(&ctx, dev)) {
		PERROR("Failed connecting to kernel watchdog driver");
		return 1;
	}

	/* Start client API socket */
	api_init(&ctx);

	/* Create pidfile when we're done with all set up. */
	pidfile_touch();

	status = uev_run(&ctx, 0);
	if (wdt_testmode())
		return status;

	api_exit();
	while (wait_reboot) {
		int reboot_in = 3 * timeout;

		INFO("Waiting for HW WDT reboot ...");
		while (reboot_in > 0) {
			unsigned int rest = sleep(timeout);

			while (rest)
				rest = sleep(rest);

			reboot_in -= timeout;
		}

		EMERG("HW WDT did not reboot, forcing reboot now ...");
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
