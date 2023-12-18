/* Tool to query status, control, and verify watchdog functionality
 *
 * Copyright (c) 2016-2023  Joachim Wiberg <troglobit@gmail.com>
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
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _LIBITE_LITE
# include <libite/lite.h>
#else
# include <lite/lite.h>
#endif

#include "config.h"
#include "private.h"
#include "wdog.h"

#define OPT_T "t:"
#define log(fmt, args...) if (verbose) fprintf(stderr, fmt "\n", ##args)

struct command {
	char  *cmd;
	int  (*cb)(char *arg);
	char  *arg;
};

extern char *__progname;

static wdog_code_t code = WDOG_FAILED_TO_MEET_DEADLINE;
static pid_t pid = 0;
static int verbose = 0;

#ifndef SUPERVISOR_TESTS_DISABLED
static int count = 1;
static int tmo   = 2000;	/* At least 2 sec timeout! */
static int false_ack = 0;
static int false_unsubscribe = 0;
static int disable_enable = 0;
static int no_kick = 0;
static int failed_kick = 0;
static int premature = 0;
#endif /* SUPERVISOR_TESTS_DISABLED */


static int do_clear(char *arg)
{
	return wdog_reset_reason_clr();
}

static int do_counter(char *arg)
{
	int rc;
	unsigned int counter = 0;

	rc = wdog_reset_counter(&counter);
	if (!rc)
		printf("%u\n", counter);

	return rc;
}

static int do_enable(char *arg)
{
	int result;

	result = wdog_enable(atoi(arg));
	if (verbose) {
		int status = 0;

		wdog_status(&status);
		printf("%s\n", status ? "Enabled" : "Disabled");
	}

	return result;
}

static int parse_code(char *arg)
{
	const char *errstr;
	int rc;

	if (!arg || arg[0] == '?' || string_match(arg, "help")) {
		printf("Reset reason codes:\n"
		       "  1   System OK\n"
		       "  2   PID failed subscribing\n"
		       "  3   PID failed kick\n"
		       "  4   PID failed unsubscribing\n"
		       "  5   PID failed to meet its deadline\n"
		       "  6   Forced reset\n"
		       "  7   Failed, unknown\n"
		       "  8   File descriptor leak\n"
		       "  9   Memory leak\n"
		       "  10  CPU overload\n");
		exit(1);
	}

	rc = strtonum(arg, 1, 10, &errstr);
	if (errstr)
		errx(1, "Error, reset reason code is %s", errstr);

	return rc;
}

static int parse_arg(char *arg, char **msg)
{
	const char *errstr;
	int msec = 0;

	if (arg && isdigit(arg[0])) {
		char *ptr;

		ptr = strchr(arg, ' ');
		if (ptr) {
			*ptr = 0;
			*msg = &ptr[1];
		}

		msec = strtonum(arg, 0, INT32_MAX, &errstr);
		if (errstr)
			errx(1, "Error, timeout value %s is %s.", arg, errstr);
	} else
		*msg = arg;

	if (!*msg || !*msg[0])
		*msg = WDOG_RESET_STR_DEFAULT;

	return msec;
}

static int do_failed(char *arg)
{
	char *msg = NULL;
	int msec = parse_arg(arg, &msg);

	return wdog_failed(code, pid, msg, msec);
}

static int do_reset(char *arg)
{
	char *msg = NULL;
	int msec = parse_arg(arg, &msg);

	return wdog_reset_timeout(pid, msg, msec);
}

static int do_reload(char *Arg)
{
	return wdog_reload();
}

static int do_debug(char *arg)
{
	int result;

	arg = wdog_get_loglevel();
	if (string_compare("notice", arg))
		arg = "debug";
	else
		arg = "notice";

	result = wdog_set_loglevel(arg);
	if (verbose)
		printf("loglevel: %s\n", wdog_get_loglevel());

	return result;
}

static int set_loglevel(char *arg)
{
	int result;

	result = wdog_set_loglevel(arg);
	if (verbose)
		printf("loglevel: %s\n", wdog_get_loglevel());

	return result;
}

static int show_status(char *arg)
{
	FILE *fp;

	fp = fopen(WDOG_STATUS, "r");
	if (fp) {
		char buf[80];

		while (fgets(buf, sizeof(buf), fp))
			fputs(buf, stdout);

		fclose(fp);
	}

	return 0;
}

static int show_version(char *arg)
{
	puts(PACKAGE_STRING);
	printf("Bug report address: %-40s\n", PACKAGE_BUGREPORT);
#ifdef PACKAGE_URL
	printf("Project homepage: %s\n", PACKAGE_URL);
#endif

	return 0;
}

#ifndef SUPERVISOR_TESTS_DISABLED
static int testit(void)
{
	int id;
	unsigned int ack;

	log("Verifying watchdog connectivity");
	if (wdog_ping())
		errx(1, "Failed connectivity check");

	log("Subscribing to process supervisor");
	id = wdog_subscribe(NULL, tmo, &ack);
	if (id < 0) {
		if (errno == EOPNOTSUPP)
			errx(1, "Cannot run tests, supervisor not enabled.");

		err(1, "Failed connecting to wdog");
		return 1; /* UNREACHED, silence Coverity/compiler */
	}

	if (false_ack)
		ack += 42;
	if (failed_kick)
		count++;	  /* catch invalid ACK on second kick */
	if (false_unsubscribe) {
		ack += 42;
		count = 0;
	}
	if (disable_enable)
		count += 10;
	if (no_kick) {
		count = 0;
		usleep(tmo * 1000 * 5);
	}

	log("Starting test loop:\n"
	    "\tcount             : %d\n"
	    "\tfalse ack         : %d\n"
	    "\tfalse unsubscribe : %d\n"
	    "\tdisable enable    : %d\n"
	    "\tno kick           : %d\n"
	    "\tpremature trigger : %d\n", count, false_ack, false_unsubscribe,
	    disable_enable, no_kick, premature);

	while (count-- > 0) {
		log("Sleeping %d msec", tmo / 2);
		usleep(tmo / 2 * 1000);

		log("Kicking watchdog: id %d, ack %u", id, ack);
		if (wdog_kick2(id, &ack))
			errx(1, "Failed kicking");

		if (count == 8)
			wdog_enable(0);
		if (count == 4)
			wdog_enable(1);
		if (failed_kick)
			ack += 42;
		if (premature)
			/* => 2000 / 2 * 1000 - 500000 = 500 ms */
			usleep(tmo / 2 * 1000 - 500000);
	}

	log("Unsubscribing: id %d, ack %u", id, ack);
	if (wdog_unsubscribe(id, ack))
		errx(1, "Failed unsubscribe");

	return 0;
}

static int run_test(char *arg)
{
	int op = -1;
	struct { char *arg; int op; } opts[] = {
		{ "complete-cycle",    200 },
		{ "disable-enable",    201 },
		{ "false-ack",         202 },
		{ "false-unsubscribe", 203 },
		{ "failed-kick",       204 },
		{ "no-kick",           205 },
		{ "premature-trigger", 206 },
		{ NULL, 0 }
	};

	for (int i = 0; opts[i].arg; i++) {
		if (string_match(opts[i].arg, arg)) {
			op = opts[i].op;
			break;
		}
	}

	switch (op) {
		case 200:
			/*
			 * Simple subscribe, kick, and unsubscribe cycle
			 */
			return testit();

		case 201:
			/*
			 * Longer test, temporarily disables watchdogd
			 * then re-enables it, and finally unsubscribes
			 */
			disable_enable = 1;
			return testit();

		case 202:
			/*
			 * Verifies bad ACK sequence in first kick
			 */
			false_ack = 1;
			return testit();

		case 203:
			/*
			 * Verifies bad ACK sequence in unsubscribe
			 */
			false_unsubscribe = 1;
			return testit();

		case 204:
			/*
			 * Verifies bad ACK sequence in second kick
			 */
			failed_kick = 1;
			return testit();

		case 205:
			/*
			 * Verifies missing kick
			 */
			no_kick = 1;
			return testit();

		case 206:
			/*
			 * With a timeout of 2 sec we normally respond
			 * once every sec.  With this we do a slightly
			 * longer response, 1.5 sec, to verify watchdogd
			 * does not trigger prematurely.
			 */
			premature = 1;
			return testit();
	}

	return -1;
}
#endif /* SUPERVISOR_TESTS_DISABLED */

static int usage(int code)
{
	printf("Usage:\n"
	       "  %s [OPTIONS] [COMMAND]\n"
	       "\n"
	       "Options:\n"
	       "  -h, --help           Display this help text and exit\n"
	       "  -c, --code=CODE      Reset reason code for fail command, -c help list codes\n"
	       "  -p, --pid=PID        PID to use for fail and reset command\n"
	       "  -v, --verbose        Verbose output, otherwise commands are silent\n"
	       "  -V, --version        Show program version\n"
	       "\n"
	       "Commands:\n"
	       "  help                 This help text\n"
	       "  debug                Toggle watchdogd debug level (notice <--> debug)\n"
	       "  loglevel LVL         Set log level: none, err, warn, notice*, info, debug\n"
	       "  fail [MSEC] [MSG]    Like reset command but can record any reset reason code.\n"
	       "                       Only performs an actual reset if MSEC is given\n"
	       "                       Default reset reason code: 5, failed to meet deadline\n"
//	       "  force-reset          Forced reset, alias to `reset 0`\n"
	       "  reset [MSEC] [MSG]   Perform system reset, optional MSEC (millisecond) delay.\n"
	       "                       The optional MSG is presented as 'label' on reboot.\n"
	       "                       Use `-p PID` option to perform reset as PID\n"
	       "  reload               Reload daemon configuration file, like SIGHUP\n"
	       "  status               Show watchdog and supervisor status, default command\n"
	       "  version              Show program version\n"
		"\n"
	       "  clear                Clear reset reason\n"
	       "  counter              Show reset counter, num. reboots since power-on\n"
		"\n"
	       "  disable              Disable watchdog\n"
	       "  enable               Re-enable watchdog\n"
		"\n"
#ifndef SUPERVISOR_TESTS_DISABLED
	       "  test    [TEST]       Run process supervisor built-in test, see below\n"
	       "\n"
	       "Tests:\n"
	       "  complete-cycle**     Verify subscribe, kick, and unsubscribe (no reset)\n"
	       "  disable-enable       Verify WDT disable, and re-enable (no reset)\n"
	       "  premature-trigger    Verify no premature trigger before unsubscribe (no reset)\n"
	       "  no-kick              Verify reset on missing first kick (reset)\n"
	       "  false-ack            Verify reset on invalid ACK in first kick (reset)\n"
	       "  failed-kick          Verify reset on invalid ACK in second kick (reset)\n"
	       "  false-unsubscribe    Verify unsubscribe with invalid ACK (reset)\n"
#endif
	       "____\n"
	       "*  default log level\n"
#ifndef SUPERVISOR_TESTS_DISABLED
	       "** default test\n"
#endif
	       "\n", __progname);

	return code;
}

static int show_usage(char *arg)
{
	return usage(0);
}

int main(int argc, char *argv[])
{
	int c;
	char *cmd, arg[120];
	struct option long_options[] = {
		{ "cause",             1, 0, 1000 },
		{ "code",              1, 0, 'c'  },
		{ "help",              0, 0, 'h'  },
		{ "pid",               1, 0, 'p'  },
		{ "verbose",           0, 0, 'v'  },
		{ "version",           0, 0, 'V'  },
		{ NULL, 0, 0, 0 }
	};
	struct command command[] = {
		{ "clear",             do_clear,     NULL },
		{ "counter",           do_counter,   NULL },
		{ "disable",           do_enable,    "0"  },
		{ "enable",            do_enable,    "1"  },
		{ "help",              show_usage,   NULL },
		{ "debug",             do_debug,     NULL },
		{ "loglevel",          set_loglevel, NULL },
		{ "failed",            do_failed,    NULL },
		{ "reboot",            do_reset,     NULL },
		{ "reset",             do_reset,     NULL },
		{ "force-reset",       do_reset,     NULL },
		{ "reload",            do_reload,    NULL },
		{ "status",            show_status,  NULL },
#ifndef SUPERVISOR_TESTS_DISABLED
		{ "test",              run_test,     NULL },
#endif
		{ "version",           show_version, NULL },
		{ NULL,                NULL,         NULL }
	};

	while ((c = getopt_long(argc, argv, "c:hp:Vv" OPT_T, long_options, NULL)) != EOF) {
		switch (c) {
		case 1000:
			warnx("Deprecated option --cause, replaced with --code");
			/* fallthrough */
		case 'c':
			code = parse_code(optarg);
			break;

		case 'h':
			return usage(0);

		case 'p':
			pid = atoi(optarg);
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			return show_version(NULL);

		default:
			warnx("Unknown or currently unsupported option '%c'.", c);
			return usage(1);
		}
	}

	if (optind >= argc)
		return show_status(NULL);

	cmd = argv[optind++];

	memset(arg, 0, sizeof(arg));
	while (optind < argc) {
		strlcat(arg, argv[optind++], sizeof(arg));
		if (optind < argc)
			strlcat(arg, " ", sizeof(arg));
	}

	for (c = 0; command[c].cmd; c++) {
		if (!string_match(command[c].cmd, cmd))
			continue;

		if (command[c].arg)
			return command[c].cb(command[c].arg);

		return command[c].cb(arg);
	}

	return usage(1);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
