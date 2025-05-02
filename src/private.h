/* Internal libwdog and watchdogd API (not for public use!)
 *
 * Copyright (c) 2015-2024  Joachim Wiberg <troglobit@gmail.com>
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

#ifndef WDOG_PRIVATE_H_
#define WDOG_PRIVATE_H_

#include <paths.h>
#include <unistd.h>

#ifndef _PATH_PRESERVE
#define _PATH_PRESERVE              "/var/lib"
#endif

#define WDOG_RESET_STR_DEFAULT      "*RESET*"

#define WDOG_STATEDIR               _PATH_PRESERVE "/misc/"
#define WDOG_STATUSDIR              _PATH_VARRUN   "watchdogd/"
#define WDOG_TESTDIR                _PATH_TMP      "watchdogd/"

#define WDOG_SOCKNAME               "sock"
#define WDOG_SUPERVISOR_PATH        WDOG_STATUSDIR WDOG_SOCKNAME
#define WDOG_SUPERVISOR_TEST        WDOG_TESTDIR   WDOG_SOCKNAME

#define WDOG_STATENAME              "watchdogd.state"
#define WDOG_STATE                  WDOG_STATEDIR  WDOG_STATENAME
#define WDOG_STATE_TEST             WDOG_TESTDIR   WDOG_STATENAME

#define WDOG_STATUSNAME             "status"
#define WDOG_STATUS                 WDOG_STATUSDIR WDOG_STATUSNAME
#define WDOG_STATUS_TEST            WDOG_TESTDIR   WDOG_STATUSNAME

#define WDOG_PIDFILE                WDOG_STATUSDIR "pid"

#define WDOG_SUBSCRIBE_CMD          1
#define WDOG_UNSUBSCRIBE_CMD        2
#define WDOG_KICK_CMD               3
#define WDOG_SET_DEBUG_CMD          4
#define WDOG_GET_DEBUG_CMD          5
#define WDOG_ENABLE_CMD             10
#define WDOG_STATUS_CMD             11
#define WDOG_RESET_CMD              12
#define WDOG_RESET_REASON_CMD       13
#define WDOG_RESET_REASON_RAW_CMD   14
#define WDOG_CLEAR_REASON_CMD       15
#define WDOG_SET_LOGLEVEL_CMD       16
#define WDOG_GET_LOGLEVEL_CMD       17
#define WDOG_RESET_COUNTER_CMD      18
#define WDOG_RELOAD_CMD             19
#define WDOG_FAILED_BASE_CMD        20
#define WDOG_FAILED_SYSTEMOK_CMD    21
#define WDOG_FAILED_SUBSCRIBE_CMD   22
#define WDOG_FAILED_KICK_CMD        23
#define WDOG_FAILED_UNSUBSCRIBE_CMD 24
#define WDOG_FAILED_DEADLINE_CMD    25
#define WDOG_FAILED_RESETNOW_CMD    26
#define WDOG_FAILED_UNKNOWN_CMD     27
#define WDOG_FAILED_FDLEAK_CMD      28
#define WDOG_FAILED_MEMLEAK_CMD     29
#define WDOG_FAILED_OVERLOAD_CMD    30
#define WDOG_LIST_SUPV_CLIENTS_CMD  31
#define WDOG_CMD_ERROR              -1

#define WDOG_SUPERVISOR_MIN_TIMEOUT 1000 /* msec */

typedef struct {
	int          cmd;
	int          error;	/* Set on WDOG_CMD_ERROR */
	unsigned int id;	/* Registered ID */
	pid_t        pid;	/* Process ID */
	unsigned int timeout;	/* msec */
	unsigned int ack;
	unsigned int next_ack;
	char         label[48];	/* process name or label */
	char         padding[128];
} wdog_t;

#endif /* WDOG_PRIVATE_H_ */

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
