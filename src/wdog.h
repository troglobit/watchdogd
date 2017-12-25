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

/* Reset cause codes */
typedef enum {
	WDOG_SYSTEM_NONE = 0,        /* XXX: After reset/power-on */
	WDOG_SYSTEM_OK,
	WDOG_FAILED_SUBSCRIPTION,
	WDOG_FAILED_KICK,
	WDOG_FAILED_UNSUBSCRIPTION,
	WDOG_FAILED_TO_MEET_DEADLINE,
	WDOG_FORCED_RESET,
	WDOG_FAILED_UNKNOWN,
	WDOG_DESCRIPTOR_LEAK,
	WDOG_MEMORY_LEAK,
	WDOG_CPU_OVERLOAD,
} wdog_cause_t;

typedef struct
{
	unsigned int  counter;
	unsigned int  wid;
	wdog_cause_t  cause;
	unsigned int  enabled;
	char          label[16];
} wdog_reason_t;

int   wdog_set_debug        (int enable);   /* Toggle debug loglevel in daemon */
int   wdog_get_debug        (int *status);  /* Check if debug is enabled */

int   wdog_set_loglevel     (char *level);
char *wdog_get_loglevel     (void);

int   wdog_enable           (int enable);   /* Attempt to temp. disable */
int   wdog_status           (int *status);  /* Check if enabled */

int   wdog_reboot           (int pid, char *label);
int   wdog_reboot_timeout   (int pid, char *label, int timeout);
int   wdog_reboot_reason    (wdog_reason_t *reason);
char *wdog_reboot_reason_str(wdog_reason_t *reason);
int   wdog_reboot_reason_clr(void);

int   wdog_pmon_ping        (void);
int   wdog_pmon_subscribe   (char *label, int timeout, int *ack); /* Returns ID or -errno */
int   wdog_pmon_unsubscribe (int id,                   int  ack); /* Returns 0 if OK, or errno */
int   wdog_pmon_extend_kick (int id,      int timeout, int *ack);
int   wdog_pmon_kick        (int id,                   int *ack); /* Returns 0 while OK, or errno */

/*
 * Compatibility wrapper layer
 */
#include "compat.h"

#endif /* WDOG_H_ */

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
