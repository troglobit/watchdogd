/* Tool to query status, control, and verify watchdog functionality
 *
 * Copyright (c) 2016  Joachim Nilsson <troglobit@gmail.com>
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

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "wdog.h"

#define log(fmt, args...) if (verbose) printf(fmt "\n", ##args)

extern char *__progname;
static int verbose = 0;
static int count = 1;
static int tmo   = 2000;	/* At least 2 sec timeout! */

static int false_ack = 0;
static int false_unsubscribe = 0;
static int disable_enable = 0;
static int no_kick = 0;
static int failed_kick = 0;
static int premature = 0;

static int do_clear(void)
{
	return wdog_reboot_reason_clr();
}

static int do_enable(int ena)
{
	int result = wdog_enable(ena);

	if (verbose) {
		int status = 0;

		wdog_status(&status);
		printf("%s\n", status ? "Enabled" : "Disabled");
	}

	return result;
}

static int do_reset(int msec)
{
	if (msec < 0)
		errx(1, "Invalid reboot timeout (%d)", msec);

	return wdog_reboot_timeout(getpid(), "*REBOOT*", msec);
}

static int set_loglevel(char *arg)
{
	int result = wdog_set_loglevel(arg);

	if (verbose)
		printf("%s\n", wdog_get_loglevel());

	return result;
}

static int show_status(void)
{
	FILE *fp;
	char *file[] = {
		WDOG_STATUS,
		WDOG_STATE,
		NULL
	};

	for (int i = 0; file[i]; i++) {
		fp = fopen(file[i], "r");
		if (fp) {
			char buf[80];

			while (fgets(buf, sizeof(buf), fp))
				fputs(buf, stdout);

			return fclose(fp);
		}
	}

	return 1;
}

static int show_version(void)
{
	return puts(PACKAGE_VERSION) != EOF;
}

static int testit(void)
{
	int id, ack;

	log("Verifying watchdog connectivity");
	if (wdog_pmon_ping())
		err(1, "Failed connectivity check");

	log("Subscribing to process supervisor");
	id = wdog_pmon_subscribe(NULL, tmo, &ack);
	if (id < 0) {
		perror("Failed connecting to pmon");
		return 1;
	}

	if (false_ack)
		ack += 42;
	if (false_unsubscribe) {
		ack += 42;
		count = 0;
	}
	if (disable_enable)
		count += 10;
	if (no_kick) {
		count = 0;
		sleep(tmo);
	}

	log("Starting test loop: count %d, false_ack %d, false_unsubscribe %d, disable_enable %d, no_kick %d",
	    count, false_ack, false_unsubscribe, disable_enable, no_kick);
	while (count-- > 0) {
		log("Sleeping %d msec", tmo / 2);
		log("Kicking watchdog: id %d, ack %d", id, ack);
		if (wdog_pmon_kick(id, &ack))
			err(1, "Failed kicking");

		if (count == 8)
			wdog_enable(0);
		if (count == 4)
			wdog_enable(1);
		if (failed_kick)
			ack += 42;
		if (premature)
			sleep(tmo / 2 - 500000);
		sleep(tmo / 2);
	}

	log("Unsubscribing: id %d, ack %d", id, ack);
	if (wdog_pmon_unsubscribe(id, ack))
		err(1, "Failed unsubscribe");

	return 0;
}

static int usage(int code)
{
	printf("Usage:\n"
	       "  %s [-cdefhsvV] [-l LEVEL] [-r MSEC]\n"
	       "\n"
	       "Commands:\n"
	       "  -c, --clear           Clear reset reason\n"
	       "  -d, --disable         Disable watchdog\n"
	       "  -e, --enable          Re-enable watchdog\n"
	       "  -f, --force-reset     Forced reset\n"
	       "  -h, --help            Display this help text and exit\n"
	       "  -l, --loglevel=LVL    Adjust daemon log level: none, err, info, notice, debug\n"
	       "  -r, --reboot=MSEC     Lke forced reset, but delays reboot MSEC milliseconds\n"
	       "  -s, --status          Show watchdog and supervisor status\n"
	       "  -v, --version         Show program version\n"
	       "  -V, --verbose         Verbose output, otherwise clear/disable etc. are silent\n"
	       "\n"
	       "Tests:\n"
	       "  --complete-cycle      Verify subscribe, kick, and unsubscribe (no reboot)\n"
	       "  --disable-enable      Verify WDT disable, and re-enable (reboot)\n"
	       "  --false-ack           Verify kick with invalid ACK (reboot)\n"
	       "  --false-unsubscribe   Verify unsubscribe with invalid ACK (reboot)\n"
	       "  --failed-kick         Verify reboot on missing kick (reboot)\n"
	       "  --no-kick             Verify reboot on missing first kick (reboot)\n"
	       "  --premature-trigger   Verify no premature trigger after re-enable (reboot)\n"
	       "\n", __progname);

	return code;
}

int main(int argc, char *argv[])
{
	int c;
	struct option long_options[] = {
		/* Options/Commands */
		{"clear",             0, 0, 'c'},
		{"disable",           0, 0, 'd'},
		{"enable",            0, 0, 'e'},
		{"force-reset",       0, 0, 'f'},
		{"loglevel",          1, 0, 'l'},
		{"help",              0, 0, 'h'},
		{"reboot",            1, 0, 'r'},
		{"status",            0, 0, 's'},
		{"verbose",           0, 0, 'V'},
		{"version",           0, 0, 'v'},
		/* Tests */
		{"complete-cycle",    0, 0, 200},
		{"disable-enable",    0, 0, 201},
		{"false-ack",         0, 0, 202},
		{"false-unsubscribe", 0, 0, 203},
		{"failed-kick",       0, 0, 204},
		{"no-kick",           0, 0, 205},
		{"premature-trigger", 0, 0, 206},
		{NULL, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "cdefl:hr:sVv?", long_options, NULL)) != EOF) {
		switch (c) {
		case 'c':
			return do_clear();

		case 'd':
			return do_enable(0);

		case 'e':
			return do_enable(1);

		case 'f':
			return do_reset(0);

		case 'l':
			return set_loglevel(optarg);

		case 'h':
			return usage(0);

		case 'r':
			return do_reset(atoi(optarg));

		case 's':
			return show_status();

		case 'V':
			verbose = 1;
			break;

		case 'v':
			return show_version();

		case 200:
			return testit();

		case 201:
			disable_enable = 1;
			return testit();

		case 202:
			false_ack = 1;
			return testit();

		case 203:
			false_unsubscribe = 1;
			return testit();

		case 204:
			failed_kick = 1;
			return testit();

		case 205:
			no_kick = 1;
			return testit();

		case 206:
			premature = 1;
			return testit();

		default:
			warn("Unknown or currently unsupported.");
			return usage(1);
		}
	}

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
