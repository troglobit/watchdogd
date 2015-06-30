/* A small userspace watchdog daemon
 *
 * Copyright (C) 2008 Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008 Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012 Joachim Nilsson <troglobit@gmail.com>
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

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <paths.h>
#include <syslog.h>
#include <sched.h>

#define WDT_DEVNODE          "/dev/watchdog"
#define WDT_TIMEOUT_DEFAULT  20
#define WDT_KICK_DEFAULT     (WDT_TIMEOUT_DEFAULT / 2)

#define UNUSED(arg) arg __attribute__((unused))
#define print(prio, fmt, args...)  (sys_log ? syslog(prio, fmt, ##args) \
                                            : fprintf(stderr, "%s: " fmt "\n", __progname, ##args))
#define ERROR(fmt, args...)                   print(LOG_DAEMON | LOG_ERR,   fmt, ##args)
#define PERROR(fmt, args...)                  print(LOG_DAEMON | LOG_ERR,   fmt ": %s", ##args, strerror(errno))
#define DEBUG(fmt, args...) do { if (verbose) print(LOG_DAEMON | LOG_DEBUG, fmt, ##args); } while(0)
#define INFO(fmt, args...)                    print(LOG_DAEMON | LOG_INFO,  fmt, ##args)
#define WARN(fmt, args...)                    print(LOG_DAEMON | LOG_WARNING, fmt, ##args)

int fd      = -1;
int magic   = 0;
int verbose = 0;
int sys_log = 0;
int extkick = 0;
int extdelay = 0;
extern char *__progname;

int daemonize(char *output);
int pidfile(const char *basename);
double check_loadavg(void);
int get_cpu_count(void);


/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
static void wdt_kick(char *msg)
{
	int dummy;

	DEBUG("%s", msg);
	ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

static void wdt_set_timeout(int count)
{
	int arg = count;

	DEBUG("Setting watchdog timeout to %d sec.", count);
	if (ioctl(fd, WDIOC_SETTIMEOUT, &arg))
		PERROR("Failed setting HW watchdog timeout");
	else
		DEBUG("Previous timeout was %d sec", arg);
}

static int wdt_get_timeout(void)
{
	int count;
	int err;

	if ((err = ioctl(fd, WDIOC_GETTIMEOUT, &count)))
		count = err;

	DEBUG("Watchdog timeout is set to %d sec.", count);

	return count;
}

static int wdt_get_bootstatus(void)
{
	int status = 0;
	int err;

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

static void wdt_close(int UNUSED(signo))
{
	if (fd != -1) {
		/* When --safe-exit is selected */
		if (magic) {
			INFO("Safe exit, disabling HW watchdog.");
			write(fd, "V", 1);
		} else
			INFO("Exiting, watchdog still active.");

		close(fd);
	}
	exit(0);
}

static void wdt_reboot(int UNUSED(signo))
{
	if (fd != -1) {
		INFO("Forced watchdog reboot.");
		wdt_set_timeout(1);
		close(fd);

		while (1)
			sched_yield();
	}
	exit(0);
}

static void wdt_external_kick(int UNUSED(signo))
{
	if (!extkick) {
		extdelay = 0;
		extkick = 1;
		INFO("External supervisor now controls watchdog kick via SIGUSR1.");
	}

	wdt_kick("External kick.");
}

static void wdt_external_kick_exit(int UNUSED(signo))
{
	INFO("External supervisor requested safe exit. Reverting to built-in kick.");
	extkick = 0;
}

static void setup_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);

	sa.sa_handler = wdt_close;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Watchdog reboot support */
	sa.sa_handler = wdt_reboot;
	sigaction(SIGPWR, &sa, NULL);

	/* Kick from external process supervisor */
	sa.sa_handler = wdt_external_kick;
	sigaction(SIGUSR1, &sa, NULL);

	/* Handle graceful exit by external supervisor */
	sa.sa_handler = wdt_external_kick_exit;
	sigaction(SIGUSR2, &sa, NULL);
}

static void create_bootstatus(int timeout, int interval)
{
	int err;
	char *status;

	err = asprintf(&status, "%s%s.status", _PATH_VARRUN, __progname);
	if (-1 != err && status) {
		FILE *fp;

		fp = fopen(status, "w");
		if (fp) {
			int cause = wdt_get_bootstatus();

			fprintf(fp, "Reset cause   : 0x%04x\n", cause >= 0 ? cause : 0);
			fprintf(fp, "Timeout (sec) : %d\n", timeout);
			fprintf(fp, "Kick interval : %d\n", interval);

			fclose(fp);
		}

		free(status);
	}
}

static int usage(int status)
{
	printf("Usage: %s [-f] [-w <sec>] [-k <sec>] [-s] [-h|--help]\n"
               "A simple watchdog deamon that kicks /dev/watchdog every %d sec, by default.\n"
               "Options:\n"
               "  --foreground, -f         Start in foreground (background is default)\n"
	       "  --external-kick, -x [N]  Force external watchdog kick using SIGUSR1\n"
	       "                           A 'N x <interval>' delay for startup is given\n"
	       "  --logfile, -l <file>     Log to <file> when backgrounding, otherwise silent\n"
	       "  --syslog, -L             Use syslog, even if in foreground\n"
               "  --timeout, -w <sec>      Set the HW watchdog timeout to <sec> seconds\n"
               "  --interval, -k <sec>     Set watchdog kick interval to <sec> seconds\n"
               "  --safe-exit, -s          Disable watchdog on exit from SIGINT/SIGTERM\n"
	       "  --load-average, -a <val> Adjust load average check, default: 0.7, reboot at 0.8\n"
	       "  --verbose, -V            Verbose operation, noisy output suitable for debugging\n"
	       "  --version, -v            Display version and exit\n"
               "  --help, -h               Display this help message and exit\n",
               __progname, WDT_TIMEOUT_DEFAULT);

	return status;
}

int main(int argc, char *argv[])
{
	double load, load_warn = .7, load_reboot =.8; /* defaults are a bit low */
	int timeout = WDT_TIMEOUT_DEFAULT;
	int real_timeout = 0;
	int period = -1;
	int background = 1;
	int c;
	char *logfile = NULL;
	struct option long_options[] = {
		{"foreground",    0, 0, 'f'},
		{"external-kick", 2, 0, 'x'},
		{"interval",      1, 0, 'k'},
		{"load-average",  1, 0, 'a'},
		{"logfile",       1, 0, 'l'},
		{"safe-exit",     0, 0, 's'},
		{"syslog",        0, 0, 'L'},
		{"timeout",       1, 0, 'w'},
		{"verbose",       0, 0, 'V'},
		{"version",       0, 0, 'v'},
		{"help",          0, 0, 'h'},
		{NULL, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "a:fx::l:Lw:k:sVvh?", long_options, NULL)) != EOF) {
		switch (c) {
		case 'a':
			load = strtod(optarg, NULL);
			if (load <= 0) {
				ERROR("Load average argument must be greater than zero.");
				return usage(1);
			}
			load_warn   = load;
			load_reboot = load + 0.1;
			break;

		case 'f':	/* Run in foreground */
			background = 0;
			break;

		case 'x':
			if (!optarg)
				extdelay = 1; /* Default is 1 x period */
			else
				extdelay = atoi(optarg);
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

		case 'w':	/* Watchdog timeout */
			if (!optarg) {
				ERROR("Missing timeout argument.");
				return usage(1);
			}
			timeout = atoi(optarg);
			break;

		case 'k':	/* Watchdog kick interval */
			if (!optarg) {
				ERROR("Missing interval argument.");
				return usage(1);
			}
			period = atoi(optarg);
			break;

		case 's':	/* Safe exit, i.e., don't reboot if we exit and close device */
			magic = 1;
			break;

		case 'v':
			printf("v%s\n", VERSION);
			return 0;

		case 'V':
			verbose = 1;
			break;

		case 'h':
			return usage(0);

		default:
			printf("Unrecognized option \"-%c\".\n", c);
			return usage(1);
		}
	}

	if (background) {
		pid_t pid;

		/* If backgrounding and no logfile is given, use syslog */
		if (!logfile)
			sys_log = 1;

		/* Exit on parent or error. */
		pid = daemonize(logfile);
		if (pid)
			return pid < 0 ? 1 : 0;
	}

	INFO("Userspace watchdog daemon v%s starting ...", VERSION);

	/* Setup callbacks for SIGUSR1 and, optionally, exit magic on SIGINT/SIGTERM */
	setup_signals();

	if (pidfile(NULL))
		PERROR("Cannot create pidfile");

	fd = open(WDT_DEVNODE, O_WRONLY);
	if (fd == -1) {
		PERROR("Failed opening watchdog device, %s", WDT_DEVNODE);
		return 1;
	}

	wdt_set_timeout(timeout);

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
	DEBUG("Watchdog kick interval set to %d sec.", period);

	/* Read boot cause from watchdog and save in /var/run/watchdogd.status */
	create_bootstatus(real_timeout, period);

	while (1) {
		int rem;

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

		/* Check CPU load average */
		load = check_loadavg();
		if (load > load_warn && load < load_reboot) {
			WARN("System load average very high!");
		} else if (load > load_reboot) {
			ERROR("System load too high, rebooting system ...");
			wdt_reboot(0);
		}

		/* Check remaining time, if awaken by signal */
		rem = period;
		do {
			rem = sleep(rem);
		} while (rem > 0);
	}
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
