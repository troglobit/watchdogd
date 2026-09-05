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

#include <errno.h>

#include "wdt.h"
#include "script.h"
#include "pressure-parser.h"

#define PRESSURE_FILE "/proc/pressure/memory"

static int logmark;
static char *exec;
static uev_t watcher;
static enum pressure_scope scope = PRESSURE_FULL;
static enum pressure_window window = PRESSURE_AVG10;
static const char *scope_name = "full";
static const char *window_name = "avg10";
static double warning;
static double critical;

static void cb(uev_t *w, void *arg, int events)
{
	struct pressure_stats stats;
	double value;

	if (pressure_read(PRESSURE_FILE, &stats)) {
		if (errno == ENOENT)
			DEBUG("Cannot read %s, PSI may not be enabled", PRESSURE_FILE);
		else
			PERROR("Cannot parse %s", PRESSURE_FILE);
		return;
	}

	value = pressure_value(&stats, scope, window) / 100.0;
	if (logmark)
		LOG("Memory pressure (%s %s): some %.2f%%, full %.2f%%",
		    scope_name, window_name,
		    pressure_value(&stats, PRESSURE_SOME, window),
		    pressure_value(&stats, PRESSURE_FULL, window));

	DEBUG("Memory pressure (%s %s): %.2f%%, warning: %.2f%%, critical: %.2f%%",
	      scope_name, window_name, value * 100, warning * 100, critical * 100);
	if (value > warning) {
		if (critical > 0.0 && value > critical) {
			EMERG("Memory pressure too high (%s %s), %.2f > %.2f, rebooting system ...",
			      scope_name, window_name, value, critical);
			if (checker_exec(exec, "pressure", 1, value, warning, critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":pressure", 0);
			return;
		}

		WARN("Memory pressure high (%s %s), %.2f > %.2f",
		     scope_name, window_name, value, warning);
		checker_exec(exec, "pressure", 0, value, warning, critical);
	}
}

int pressure_init(uev_ctx_t *ctx, const char *selected_scope,
		  const char *selected_window, int T, int mark,
		  float warn, float crit, char *script)
{
	enum pressure_scope new_scope;
	enum pressure_window new_window;

	if (!selected_scope)
		selected_scope = "full";
	if (!selected_window)
		selected_window = "avg10";

	if (pressure_scope_parse(selected_scope, &new_scope)) {
		ERROR("Invalid memory pressure scope '%s', expected 'some' or 'full'",
		      selected_scope);
		return 1;
	}
	if (pressure_window_parse(selected_window, &new_window)) {
		ERROR("Invalid memory pressure window '%s', expected 'avg10', 'avg60', or 'avg300'",
		      selected_window);
		return 1;
	}

	if (!T) {
		INFO("Memory pressure monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	scope = new_scope;
	window = new_window;
	scope_name = scope == PRESSURE_SOME ? "some" : "full";
	window_name = window == PRESSURE_AVG10 ? "avg10" :
		window == PRESSURE_AVG60 ? "avg60" : "avg300";
	logmark = mark;
	warning = warn;
	critical = crit;
	free(exec);
	exec = script ? strdup(script) : NULL;

	INFO("Memory pressure monitor, period %d sec, %s %s, warning: %.2f%%, reboot: %.2f%%",
	     T, scope_name, window_name, warning * 100, critical * 100);

	uev_timer_stop(&watcher);
	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
