/* Watchdog API for reset cause, file store backend
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

int reset_cause_set(pid_t pid, wdog_reason_t *reason)
{
	FILE *fp;
	const char *state;

	if (wdt_testmode())
		state = WDOG_STATE_TEST;
	else
		state = WDOG_STATE;

	fp = fopen(state, "w");
	if (!fp) {
		PERROR("Failed opening %s to save reset cause %s[%d]: %s",
		       state, reason->label, pid, wdog_reboot_reason_str(reason));
		return 1;
	}

	if (!reason->label[0])
		strlcpy(reason->label, "XBAD_LABEL", sizeof(reason->label));

	fprintf(fp, WDT_REASON_CNT ": %u\n", reason->counter);
	fprintf(fp, WDT_REASON_PID ": %d\n", pid);
	fprintf(fp, WDT_REASON_WID ": %d\n", reason->wid);
	fprintf(fp, WDT_REASON_LBL ": %s\n", reason->label);
	fprintf(fp, WDT_REASON_CSE ": %d\n", reason->cause);
	fprintf(fp, WDT_REASON_STR ": %s\n", wdog_reboot_reason_str(reason));
	fclose(fp);

	return 0;
}

int reset_cause_get(wdog_reason_t *reason)
{
	FILE *fp;
	char buf[80];
	const char *state;

	if (!reason)
		return errno = EINVAL;

	if (wdt_testmode())
		state = WDOG_STATE_TEST;
	else
		state = WDOG_STATE;

	/* Clear contents to handle first boot */
	memset(reason, 0, sizeof(*reason));

	fp = fopen(state, "r");
	if (!fp) {
		if (errno != ENOENT) {
			PERROR("Failed opening %s to read reset cause", state);
			return 1;
		}
		return 0;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if (sscanf(buf, WDT_REASON_CNT ": %u\n", &reason->counter) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_WID ": %d\n", &reason->wid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_CSE ": %d\n", (int *)&reason->cause) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_LBL ": %s\n", reason->label) == 1)
			continue;
	}
	fclose(fp);

	return 0;
}

int reset_cause_clear(void)
{
	wdog_reason_t reason;

	memset(&reason, 0, sizeof(reason));
	return reset_cause_set(0, &reason);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
