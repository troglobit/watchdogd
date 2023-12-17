/* Watchdog API for the process supervisor, its clients, and others
 *
 * Copyright (C) 2015-2023  Joachim Wiberg <troglobit@gmail.com>
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

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef WDOG_H_
#define WDOG_H_

#include <time.h>

/* Reset reason codes */
typedef enum {
	WDOG_SYSTEM_NONE = 0,	      /* After reset/power-on */
	WDOG_SYSTEM_OK,		      /* Unused? */
	WDOG_FAILED_SUBSCRIPTION,     /* Supervised process */
	WDOG_FAILED_KICK,	      /* Supervised process */
	WDOG_FAILED_UNSUBSCRIPTION,   /* Supervised process */
	WDOG_FAILED_TO_MEET_DEADLINE, /* Supervised process */
	WDOG_FORCED_RESET,	      /* Operator requested system reboot */
	WDOG_FAILED_UNKNOWN,	      /* Likely, WDT timed out*/
	WDOG_DESCRIPTOR_LEAK,	      /* filenr  pluing */
	WDOG_MEMORY_LEAK,	      /* meminfo plugin */
	WDOG_CPU_OVERLOAD,	      /* loadavg plugin */
} wdog_code_t;

typedef struct
{
	unsigned int  counter;   /* Global reset counter, not per-reason */
	unsigned int  wid;       /* Watchdog ID of process causing reset */
	wdog_code_t   code;      /* Reset reason code, use wdog_reset_reason_str() */
	unsigned int  enabled;   /* Unused, kept for compat. */
	char          label[48]; /* Process name causing reset, or label */
	struct tm     date;      /* Recorded time of reset */
} wdog_reason_t;

int   wdog_set_debug        (int enable);   /* Toggle debug loglevel in daemon */
int   wdog_get_debug        (int *status);  /* Check if debug is enabled */

int   wdog_set_loglevel     (char *level);
char *wdog_get_loglevel     (void);

int   wdog_enable           (int enable);   /* Attempt to temp. disable */
int   wdog_status           (int *status);  /* Check if enabled */

int   wdog_failed           (wdog_code_t code, int pid, char *label, unsigned int timeout);

int   wdog_reset            (int pid, char *label);
int   wdog_reset_timeout    (int pid, char *label, unsigned int timeout);
int   wdog_reset_counter    (unsigned int *counter);
int   wdog_reset_reason     (wdog_reason_t *reason);
int   wdog_reset_reason_raw (wdog_reason_t *reason);
char *wdog_reset_reason_str (wdog_reason_t *reason);
int   wdog_reset_reason_clr (void);

/*
 * Check if watchdogd API is actively responding,
 * returns %TRUE(1) or %FALSE(0)
 */
int   wdog_ping             (void);
int   wdog_reload           (void);

/*
 * Process supervisor API, see also compat.h
 */

/**
 * Start supervising a subscriber
 *
 * After this, one of the kick functions must be called at least every `timeout` ms until
 * wdog_unsubscribe is called.  If not, watchdogd will (depending on the configuration)
 * reset the system or call the supervisor script.
 *
 * @param label Name of this subscriber. If NULL, process ID will be used.
 * @param timeout Timeout in ms
 * @param[out] next_ack out-parameter - the value must be passed to next API call
 * @return ID on success, negative on error (also sets @param errno)
 */
int   wdog_subscribe        (char *label, unsigned int timeout, unsigned int *next_ack);

/**
 * Stop supervising a subscriber
 * Checks ack and stops supervisor for this subscriber
 * @param id return value from wdog_subscribe
 * @param ack Last ack received from the wdog API
 * @return 0 on success, negative on error (also sets @param errno)
 */
int   wdog_unsubscribe      (int id, unsigned int ack);

/**
 * Kick the watchdog with a custom timeout (old API)
 * Checks ack, restarts timer with provided timeout and sets next_ack
 * @param id return value from wdog_subscribe
 * @param timeout Number of ms to set timeout to
 * @param ack ack received from last wdog API call
 * @param[out] next_ack ack to pass to next wdog API call
 * @return 0 on success, negative on error (also sets @param errno)
 */
int   wdog_kick             (int id, unsigned int timeout, unsigned int ack, unsigned int *next_ack);

/**
 * Kick the watchdog with a custom timeout
 * Checks ack, restarts timer with provided timeout and sets ack
 * @param id return value from wdog_subscribe
 * @param timeout Number of ms to set timeout to
 * @param[in,out] ack Pointer to ack received from last wdog API call.  Will be updated with new ack.
 * @return 0 on success, negative on error (also sets @param errno)
 */
int   wdog_extend_kick      (int id, unsigned int timeout, unsigned int *ack);

/**
 * Kick the watchdog
 *
 * Checks ack, restarts timer and sets next_ack
 * Uses the timeout value provided in wdog_subscribe
 *
 * @param id The ID returned from wdog_subscribe()
 * @param[in,out] ack Pointer to ack received from last wdog API call.  Will be updated with new ack.
 * @return 0 on success, negative on error (also sets @param errno)
 */
int   wdog_kick2            (int id, unsigned int *ack);

/*
 * Compatibility wrapper layer
 */
#include "compat.h"

#endif /* WDOG_H_ */

#ifdef __cplusplus
}
#endif

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
