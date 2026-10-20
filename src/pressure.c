/* Memory pressure monitor
 *
 * Copyright (C) 2026  The watchdogd authors
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

#include <math.h>

#include "wdt.h"
#include "script.h"

#define PRESSURE_FILE "/proc/pressure/memory"

static int logmark;
static char *exec;
static uev_t watcher;
static const char *scopes[]  = { "some", "full" };
static const char *windows[] = { "avg10", "avg60", "avg300" };
static int scope = 1;
static int window;
static double warning;
static double critical;

static void cb(uev_t *w, void *arg, int events)
{
	char buf[256], name[16];
	double avg[3], value = -1.0;
	FILE *fp;

	fp = fopen(PRESSURE_FILE, "r");
	if (!fp) {
		DEBUG("Cannot read %s, PSI may not be enabled", PRESSURE_FILE);
		return;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if (sscanf(buf, "%15s avg10=%lf avg60=%lf avg300=%lf",
			   name, &avg[0], &avg[1], &avg[2]) != 4)
			continue;
		if (!strcmp(name, scopes[scope])) {
			value = avg[window];
			break;
		}
	}
	fclose(fp);

	if (!isfinite(value) || value < 0.0 || value > 100.0) {
		DEBUG("Cannot read %s %s from %s", scopes[scope], windows[window], PRESSURE_FILE);
		return;
	}

	if (logmark)
		LOG("Memory pressure (%s %s): %.2f%%", scopes[scope], windows[window], value);

	value /= 100.0;
	DEBUG("Memory pressure (%s %s): %.2f%%, warning: %.2f%%, critical: %.2f%%",
	      scopes[scope], windows[window], value * 100, warning * 100, critical * 100);
	if (value > warning) {
		if (critical > 0.0 && value > critical) {
			EMERG("Memory pressure too high (%s %s), %.2f%% > %.2f%%, rebooting system ...",
			      scopes[scope], windows[window], value * 100, critical * 100);
			if (checker_exec(exec, "pressure", 1, value, warning, critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":pressure", 0);
			return;
		}

		WARN("Memory pressure high (%s %s), %.2f%% > %.2f%%",
		     scopes[scope], windows[window], value * 100, warning * 100);
		checker_exec(exec, "pressure", 0, value, warning, critical);
	}
}

void pressure_select(int sc, int win)
{
	scope = sc;
	window = win;
}

int pressure_init(uev_ctx_t *ctx, const char *name, int T, int mark,
		  float warn, float crit, char *script)
{
	(void)name;

	if (!T) {
		INFO("Memory pressure monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	INFO("Memory pressure monitor, period %d sec, %s %s, warning: %.2f%%, reboot: %.2f%%",
	     T, scopes[scope], windows[window], warn * 100, crit * 100);
	logmark = mark;
	warning = warn;
	critical = crit;
	free(exec);
	exec = script ? strdup(script) : NULL;

	uev_timer_stop(&watcher);
	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
