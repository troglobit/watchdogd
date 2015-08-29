/* Watchdog API for pmon and its clients
 *
 * Copyright (c) 2015  Joachim Nilsson <troglobit@gmail.com>
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

#include <unistd.h>

#define WDOG_PMON_PATH              "/tmp/wdog.sock"

#define WDOG_PMON_SUBSCRIBE_CMD     1
#define WDOG_PMON_UNSUBSCRIBE_CMD   2
#define WDOG_PMON_KICK_CMD          3
#define WDOG_PMON_CMD_ERROR         255

#define WDOG_PMON_MIN_TIMEOUT 1000 /* msec */

typedef struct {
	int    cmd;
	int    id;		/* Registered ID */
	pid_t  pid;		/* Process ID */
	char   label[16];	/* process name or label */
	int    timeout;		/* msec */
	int    ack, next_ack;
} wdog_pmon_t;

int wdog_pmon_ping        (void);
int wdog_pmon_subscribe   (char *label, int timeout, int *ack); /* Returns ID or -errno */
int wdog_pmon_unsubscribe (int id, int ack);  /* Returns 0 if OK, or errno */
int wdog_pmon_kick        (int id, int *ack); /* Returns 0 while OK, or errno */


/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
