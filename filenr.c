/* File descriptor monitor
 *
 * Copyright (C) 2015  Joachim Nilsson <troglobit@gmail.com>
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

#define PROC_FILE "/proc/sys/fs/file-nr"

static uev_t watcher;

/* Default: enabled */
static double warning  = 0.8;	/* 80% of all file descriptors allocated */
static double critical = 0.95;


static void cb(uev_t *w, void *UNUSED(arg), int UNUSED(events))
{
	char *ptr, buf[80];
	FILE *fp;
	double level;
	uint32_t curr = 0, un = 0, max = 0;

	fp = fopen(PROC_FILE, "r");
	if (!fp) {
		DEBUG("Cannot read %s, maybe /proc is not mounted yet", PROC_FILE);
		return;
	}

	/*
	 * $ cat /proc/sys/fs/file-nr 
         *   16768	0	803031
	 */
	ptr = fgets(buf, sizeof(buf), fp);
	fclose(fp);

	if (!ptr) {
		WARN("Failed reading contents from %s", PROC_FILE);
		return;
	}

	sscanf(buf, "%d\t%d\%d", &curr, &un, &max);
	level = (double)(curr - un) / max;
	DEBUG("Current file-nr: %d max: %d, level: %.0f%%, warning: %.0f%%, critical: %.0f%%",
	      curr, max, level * 100, warning * 100, critical * 100);

	if (level > warning) {
		if (level > critical) {
			ERROR("File descriptor usage too high, rebooting system ...");
			wdt_reboot(w->ctx);
			return;
		}

		WARN("File descriptor use very high, possible leak!");
	}
}

int filenr_init(uev_ctx_t *ctx, int T)
{
	if (warning == 0.0 && critical == 0.0) {
		INFO("File descriptor monitoring disabled.");
		return 1;
	}

	INFO("Starting file descriptor monitor, warning: %.0f%%, reboot: %.0f%%",
	     warning * 100, critical * 100);

	return uev_timer_init(ctx, &watcher, cb, NULL, T, T);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
