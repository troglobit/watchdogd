/* A small userspace watchdog daemon
 *
 * Copyright (C) 2008 Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008 Mike Frysinger <vapier@gentoo.org>
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

#define WDT_DEVNODE          "/dev/watchdog"
#define WDT_TIMEOUT_DEFAULT  20
#define WDT_KICK_DEFAULT     (WDT_TIMEOUT_DEFAULT / 2)

#define UNUSED(arg) arg __attribute__((unused))
#define ERROR(fmt, args...)              fprintf(stderr, "%s: " fmt, __progname, ##args)
#define PERROR(fmt, args...)             fprintf(stderr, "%s: " fmt ": %s\n", __progname, ##args, strerror(errno))
#define PRINT(fmt, args...) if (verbose) fprintf(stderr, "%s: " fmt, __progname, ##args)

int fd = -1;
int verbose = 0;
extern char *__progname;


/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
static void wdt_kick(void)
{
	int dummy;

	PRINT("Kicking watchdog.\n");
	ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

static void wdt_set_timeout(int count)
{
	int arg = count;

	PRINT("Setting watchdog timeout to %d sec.\n", count);
	if (ioctl(fd, WDIOC_SETTIMEOUT, &arg))
		PERROR("Failed setting HW watchdog timeout");
	else
		PRINT("Previous timeout was %d sec\n", arg);
}

static int wdt_get_timeout(void)
{
	int count;
	int err;

	if ((err = ioctl(fd, WDIOC_GETTIMEOUT, &count)))
		count = err;

	PRINT("Watchdog timeout is set to %d sec.\n", count);

	return count;
}

static void wdt_magic_close(int UNUSED(signo))
{
	if (fd != -1) {
		PRINT("Safe exit, disabling HW watchdog.\n");
		write(fd, "V", 1);
		close(fd);
	}
	exit(0);
}

static void setup_magic_close(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = wdt_magic_close;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static void daemonize(int nochdir, int noclose, int argc, char **argv)
{
	int f;
	char **vfork_args;
	int a = 0;

	setsid();

	if (!nochdir)
		chdir("/");

	if (!noclose && (f = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(f, STDIN_FILENO);
		dup2(f, STDOUT_FILENO);
		dup2(f, STDERR_FILENO);
		if (f > 2)
			close(f);
	}

	vfork_args = malloc(sizeof(char *) * (argc + 2));
	while (*argv) {
		vfork_args[a++] = *argv;
		argv++;
	}
	vfork_args[a++] = "-f";
	vfork_args[a++] = NULL;
	switch (vfork()) {
	case 0:		/* child */
		/* Make certain we are not a session leader, or else we
		 * might reacquire a controlling terminal */
		if (vfork())
			_exit(0);
		execvp(vfork_args[0], vfork_args);
		PERROR("execv");
		exit(1);

	case -1:	/* error */
		PERROR("vfork");
		exit(1);

	default:	/* parent */
		exit(0);
	}
}

static void usage(void)
{
	printf("Usage: %s [-f] [-w <sec>] [-k <sec>] [-s] [-h|--help]\n"
               "A simple watchdog deamon that kicks /dev/watchdog every %d sec, by default.\n"
               "Options:\n"
               "  --foreground, -f       Start in foreground (background is default)\n"
               "  --timeout, -w <sec>    Set the HW watchdog timeout to <sec> seconds\n"
               "  --period, -k <sec>     Set watchdog kick period to <sec> seconds\n"
               "  --safe-exit, -s        Disable watchdog on exit from SIGINT/SIGTERM\n"
	       "  --verbose, -V          Verbose operation, noisy output suitable for debugging\n"
	       "  --version, -v          Display daemon version and exit\n"
               "  --help, -h             Display this help message and exit\n",
               __progname, WDT_TIMEOUT_DEFAULT);
}

int main(int argc, char *argv[])
{
	int timeout = WDT_TIMEOUT_DEFAULT;
	int real_timeout = 0;
	int period = -1;
	int background = 1;
	int c;
	struct option long_options[] = {
		{"foreground", 0, 0, 'f'},
		{"period",     1, 0, 'w'},
		{"heartbeat",  1, 0, 'k'},
		{"safe-exit",  0, 0, 's'},
		{"verbose",    0, 0, 'V'},
		{"version",    0, 0, 'v'},
		{"help",       0, 0, 'h'},
		{NULL, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "fw:k:svVh?", long_options, NULL)) != EOF) {
		switch (c) {
		case 'f':
			background = 0;
			break;

		case 'w':
			timeout = atoi(optarg);
			break;

		case 'k':
			period = atoi(optarg);
			PRINT("Watchdog kick interval set to %d sec.\n", period);
			break;

		case 's':
			setup_magic_close();
			break;

		case 'v':
			PRINT("v%s\n", VERSION);
			exit(0);
			break;

		case 'V':
			verbose = 1;
			break;

		case 'h':
			usage();
			exit(0);
			break;

		default:
			PRINT("Unrecognized option \"-%c\".\n", c);
			usage();
			exit(1);
			break;
		}
	}

	if (background) {
		PRINT("Starting in deamon mode.\n");
		daemonize(1, 0, argc, argv);
	}

	fd = open(WDT_DEVNODE, O_WRONLY);
	if (fd == -1) {
		PERROR("Failed opening watchdog device, %s", WDT_DEVNODE);
		exit(1);
	}

	wdt_set_timeout(timeout);

	real_timeout = wdt_get_timeout();
	if (real_timeout < 0) {
		PERROR("Failed reading current watchdog timeout");
	} else {
		if (real_timeout <= period) {
			ERROR("Warning, watchdog timeout <= kick interval: %d <= %d\n",
			      real_timeout, period);
		}
	}

	/* If user did not provide '-k' argument, set to half actual timeout */
	if (-1 == period) {
		if (real_timeout < 0)
			period = WDT_KICK_DEFAULT;
		else
			period = real_timeout / 2;

		PRINT("Watchdog kick interval set to %d sec.\n", period);
	}

	while (1) {
		wdt_kick();
		sleep(period);
	}
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
