/* Watchdog API for pmon and its clients
 *
 * Copyright (c) 2015-2016  Joachim Nilsson <troglobit@gmail.com>
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

#ifndef WDOG_H_
#define WDOG_H_

#include <paths.h>
#include <unistd.h>

#define WDOG_PMON_BASENAME          "watchdogd.sock"
#define WDOG_PMON_PATH              _PATH_VARRUN WDOG_PMON_BASENAME
#define WDOG_PMON_TEST              _PATH_TMP    WDOG_PMON_BASENAME

#define WDOG_PMON_SUBSCRIBE_CMD     1
#define WDOG_PMON_UNSUBSCRIBE_CMD   2
#define WDOG_PMON_KICK_CMD          3
#define WDOG_SET_DEBUG_CMD          4
#define WDOG_GET_DEBUG_CMD          5
#define WDOG_ENABLE_CMD             10
#define WDOG_STATUS_CMD             11
#define WDOG_REBOOT_CMD             12
#define WDOG_PMON_CMD_ERROR         255

#define WDOG_PMON_MIN_TIMEOUT       1000 /* msec */

/* Reset cause codes */
typedef enum {
	WDOG_SYSTEM_NONE = 0,        /* XXX: After reset/power-on */
	WDOG_SYSTEM_OK,
	WDOG_FAILED_SUBSCRIPTION,
	WDOG_FAILED_KICK,
	WDOG_FAILED_UNSUBSCRIPTION,
	WDOG_FAILED_TO_MEET_DEADLINE,
	WDOG_FORCED_RESET,
	WDOG_DESCRIPTOR_LEAK,
	WDOG_MEMORY_LEAK,
	WDOG_CPU_OVERLOAD,
	WDOG_FAILED_UNKNOWN,
} wdog_cause_t;

typedef struct
{
   unsigned int  counter;
   unsigned int  wid;
   wdog_cause_t  cause;
   unsigned int  enabled;
   char          label[16];
} wdog_reason_t;

typedef struct {
	int    cmd;
	int    error;		/* Set on WDOG_PMON_CMD_ERROR */
	int    id;		/* Registered ID */
	pid_t  pid;		/* Process ID */
	char   label[16];	/* process name or label */
	int    timeout;		/* msec */
	int    ack, next_ack;
} wdog_pmon_t;

int wdog_set_debug        (int enable);   /* Toggle debug loglevel in daemon */
int wdog_get_debug        (int *status);  /* Check if debug is enabled */

int wdog_enable           (int enable);   /* Attempt to temp. disable */
int wdog_status           (int *status);  /* Check if enabled */

int wdog_reboot           (pid_t pid, char *label);

int wdog_pmon_ping        (void);
int wdog_pmon_subscribe   (char *label, int timeout, int *ack); /* Returns ID or -errno */
int wdog_pmon_unsubscribe (int id, int ack);  /* Returns 0 if OK, or errno */
int wdog_pmon_extend_kick (int id, int timeout, int *ack);
int wdog_pmon_kick        (int id, int *ack); /* Returns 0 while OK, or errno */

#endif /* WDOG_H_ */

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
