/* Watchdog API for reset reason, file store backend
 *
 * Copyright (C) 2012-2017  Joachim Nilsson <troglobit@gmail.com>
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

#include "wdt.h"
#include <stdio.h>

static int rrenabled = 0;
static char *rrfile  = NULL;


int reset_reason_init(int enabled, char *file)
{
	if (rrfile)
		free(rrfile);

	if (!file)
		rrfile = strdup(WDOG_STATE);
	else
		rrfile = strdup(file);
	rrenabled = enabled;

	return 0;
}

int reset_reason_set(wdog_reason_t *reason, pid_t pid)
{
	FILE *fp;
	const char *state;

	if (wdt_testmode())
		state = WDOG_STATE_TEST;
	else
		state = rrfile;

	if (!rrenabled || !rrfile) {
		DEBUG("Reset cause back-end disabled.");
		return 0;
	}

	fp = fopen(state, "w");
	if (!fp) {
		PERROR("Failed opening %s to save reset cause %s[%d]: %s",
		       state, reason->label, pid, wdog_reset_reason_str(reason));
		return 1;
	}

	if (wdt_fstore_reason(fp, reason, pid))
		PERROR("Failed writing reset cause to disk");

	return 0;
}

int reset_reason_get(wdog_reason_t *reason, pid_t *pid)
{
	FILE *fp;
	const char *state;

	if (!reason)
		return errno = EINVAL;

	if (wdt_testmode())
		state = WDOG_STATE_TEST;
	else
		state = rrfile;

	/* Clear contents to handle first boot */
	memset(reason, 0, sizeof(*reason));

	if (!rrenabled || !rrfile) {
		DEBUG("Reset cause back-end disabled.");
		return 0;
	}

	fp = fopen(state, "r");
	if (!fp) {
		if (errno != ENOENT) {
			PERROR("Failed opening %s to read reset cause", state);
			return 1;
		}
		return 0;
	}

	return wdt_fload_reason(fp, reason, pid);
}

/*
 * This function takes an argument since requirements may dictate
 * that the back-end is reset to certain values, not just zero.
 *
 * In the general case, vanilla watchdogd, we always want to step
 * the reset counter.  E.g. to detect sudden power-loss.  So this
 * function is then called with all fields cleared but counter.
 */
int reset_reason_clear(wdog_reason_t *r)
{
	wdog_reason_t reason;

	memset(&reason, 0, sizeof(reason));
	if (!r)
		r = &reason;

	return reset_reason_set(r, 0);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
