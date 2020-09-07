/* CPU load average monitor
 *
 * Copyright (C) 2015       Christian Lockley <clockley1@gmail.com>
 * Copyright (C) 2015-2020  Joachim Wiberg <troglobit@gmail.com>
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

#include <sys/sysinfo.h>
#include "wdt.h"
#include "script.h"

static int logmark = 0;
static char *exec = NULL;
static uev_t watcher;

/* Default: disabled -- recommended 0.8, 0.9 */
static double warning  = 0.0;
static double critical = 0.0;

#if 0
static void compensate(double load[])
{
	static double num = 0.0;

	if (num == 0.0) {
		num = (double)get_nprocs();
		if (num == 0.0)
			num = 1.0;
	}

	for (int i = 0; i < 3; i++)
		load[i] /= num;
}
#endif

static int above_watermark(double avg, struct sysinfo *si)
{
	/* Expect loadavg to be out of wack first five mins after boot. */
	if (si->uptime < 300)
		return 0;

	/* High watermark alert disabled */
	if (critical == 0.0)
		return 0;

	if (avg <= critical)
		return 0;

	return 1;
}

static void cb(uev_t *w, void *arg, int events)
{
	double avg, load[3];
	struct sysinfo si;

	if (sysinfo(&si)) {
		ERROR("Failed reading system loadavg");
		return;
	}

	for (int i = 0; i < 3; i++)
		load[i] = (double)si.loads[i] / (1 << SI_LOAD_SHIFT);

#ifdef SYSLOG_MARK
//	LOG("Load avg: %.2f, %.2f, %.2f (1, 5, 15 min) | Num CPU cores: %d",
//	    load[0], load[1], load[2], (int)num);
	if (logmark)
		LOG("Loadavg: %.2f, %.2f, %.2f (1, 5, 15 min)", load[0], load[1], load[2]);
#endif

#if 0
	/* Compensate for number of CPU cores */
	compensate(load);
#endif

	avg = (load[0] + load[1]) / 2.0;
	DEBUG("System load: %.2f, %.2f, %.2f (1, 5, 15 min), avg: %.2f (1 + 5), warning: %.2f, reboot: %.2f",
	      load[0], load[1], load[2], avg, warning, critical);

	if (avg > warning) {
		if (above_watermark(avg, &si)) {
			EMERG("System load too high, %.2f > %0.2f, rebooting system ...", avg, critical);
			if (checker_exec(exec, "loadavg", 1, avg, warning, critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":loadavg", 0);
			return;
		}

		WARN("System load average very high, %.2f > %0.2f!", avg, warning);
		checker_exec(exec, "loadavg", 0, avg, warning, critical);
	}
}

/*
 * Every T seconds we check loadavg
 * First run is after 1 sec on init, then every period seconds
 */
int loadavg_init(uev_ctx_t *ctx, int T, int mark, float warn, float crit, char *script)
{
	if (!T) {
		INFO("Load average monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	INFO("Load average monitor, period %d sec, warning: %.2f, reboot: %.2f", T, warn, crit);
	logmark = mark;
	warning = warn;
	critical = crit;
	if (script) {
		if (exec)
			free(exec);
		exec = strdup(script);
	}

	uev_timer_stop(&watcher);
	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
