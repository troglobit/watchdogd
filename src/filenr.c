/* File descriptor monitor
 *
 * Copyright (C) 2015-2018  Joachim Nilsson <troglobit@gmail.com>
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

#include "plugin.h"

#define PROC_FILE "/proc/sys/fs/file-nr"

static int logmark = 0;
static uev_t watcher;

/* Default: disabled -- recommended 0.8, 0.95 */
static double warning  = 0.0;
static double critical = 0.0;


static void cb(uev_t *w, void *arg, int events)
{
	char *ptr, buf[80];
	FILE *fp;
	double level;
	uint32_t curr = 0, un = 0, max = 0;

	fp = fopen(PROC_FILE, "r");
	if (!fp) {
		PERROR("Cannot read %s, maybe /proc is not mounted", PROC_FILE);
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

#ifdef SYSLOG_MARK
//	LOG("Current file-nr: %d max: %d, level: %.0f%%, warning: %.0f%%, critical: %.0f%%",
//	    curr, max, level * 100, warning * 100, critical * 100);
	if (logmark)
		LOG("File nr: %d/%d", curr, max);
#endif

	if (level > warning) {
		if (critical > 0.0 && level > critical) {
			ERROR("File descriptor usage too high, %.2f > %0.2f, rebooting system ...", level, critical);
			if (script_exec("filenr", 1, level, warning, critical))
				wdt_forced_reboot(w->ctx, getpid(), wdt_plugin_label("filenr"), 0);
			return;
		}

		WARN("File descriptor use very high, %.2f > %0.2f, possible leak!", level, warning);
		script_exec("filenr", 0, level, warning, critical);
	}
}

int filenr_init(uev_ctx_t *ctx, int T, int mark, float warn, float crit)
{
	if (!T) {
		INFO("File descriptor leak monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	INFO("File descriptor leak monitor, period %d sec, warning: %.2f%%, reboot: %.2f%%",
	     T, warning * 100, critical * 100);
	logmark = mark;
	warning = warn;
	critical = crit;

	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
