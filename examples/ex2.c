/* Another wdog client example, press Ctrl-C to exit ...
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../wdog.h"

extern char *__progname;
extern int   __wdog_testmode;

#define DEBUG(fmt, args...)  if (dbg) printf("%s: " fmt "\n", __progname, ##args);
#define PERROR(fmt, args...) if (dbg) fprintf(stderr, "%s: " fmt ": %s\n", __progname, ##args, strerror(errno));

int main(int argc, char *argv[])
{
	int id, i;
	int ack = -1, dbg = 0;
	wdog_reason_t reason;

	if (argc >= 2 && !strncmp(argv[1], "-V", 2))
		dbg = 1;

	DEBUG("Checking connectivity with watchdogd ...");
	if (wdog_pmon_ping()) {
		PERROR("Failed connectivity check");
		return 1;
	}
	wdog_set_debug(dbg);
	wdog_get_debug(&ack);
	DEBUG("OK (debug:%d)!", ack);

	wdog_reboot_reason(&reason);
	printf("Reset reason: %s\n", wdog_reboot_reason_str(&reason));
	
	id = wdog_pmon_subscribe(NULL, 2000, &ack);
	if (id < 0) {
		perror("Failed connecting to pmon");
		return 1;
	}

	printf("Press Ctrl-C to exit.\n");
	for (i = 0; i < 200; i++) {
		DEBUG("Kicking ...");
		if (wdog_pmon_kick(id, &ack))
			PERROR("Failed kicking");

		sleep(1);
	}

	DEBUG("Exiting ...");
	if (wdog_pmon_unsubscribe(id, ack)) {
		PERROR("Failed unsubscribe");
		return 1;
	}

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
