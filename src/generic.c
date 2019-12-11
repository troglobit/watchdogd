/* Generic script monitor
 *
 * Copyright (C) 2018  Tom Deblauwe <deblauwetom@gmail.com>
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

#include <sys/wait.h>
#include <unistd.h>

#include "wdt.h"
#include "script.h"

typedef struct {
	uev_t  watcher;

	int    is_running;
	int    warning;
	int    critical;

	uev_t  script_watcher;
	int    script_runtime;
	int    script_runtime_max;
	char  *script;
	char  *exec;
	pid_t  pid;
} generic_t;


static void generic_cb(uev_t *w, void *arg, int events)
{
	generic_t *gs = (generic_t *)arg;
	int status;

	DEBUG("Verifying if monitor script PID %d is still running", gs->pid);
	status = exit_code(gs->pid);
	if (status >= 0) {
		uev_timer_stop(&gs->script_watcher);
		gs->is_running = 0;

		if (status >= gs->critical) {
			EMERG("Monitor script returned critical: %d, rebooting system ...", status);
			if (checker_exec(gs->exec, "generic", 1, status, gs->warning, gs->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);

			return;
		}

		if (status >= gs->warning) {
			WARN("Monitor script returned warning: %d", status);
			checker_exec(gs->exec, "generic", 0, status, gs->warning, gs->critical);
		}

		return;
	}

	gs->script_runtime += 1000;
	if (gs->script_runtime >= gs->script_runtime_max) {
		ERROR("Monitor script PID %d still running after %d sec", gs->pid, gs->script_runtime_max);
		if (checker_exec(gs->exec, "generic", 1, 255, gs->warning, gs->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
	}
}

static int runit(uev_t *w, generic_t *gs)
{
	pid_t pid;

	uev_timer_stop(&gs->script_watcher);

	pid = generic_exec(gs->script, gs->warning, gs->critical);
	if (pid <= 0) {
		ERROR("Could not start monitor script %s", gs->script);
		return -1;
	}

	gs->pid = pid;
	gs->is_running = 1;
	gs->script_runtime = 0;

	uev_timer_init(w->ctx, &gs->script_watcher, generic_cb, gs, 1000, 1000);

	return gs->pid;
}

static void cb(uev_t *w, void *arg, int events)
{
	generic_t *gs = (generic_t *)arg;

	if (!gs)
		return;

	if (gs->is_running) {
		EMERG("Timeout reached, script %s is still running, rebooting system ...", gs->script);
		if (checker_exec(gs->exec, "generic", 1, 100, gs->warning, gs->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		return;
	}

	if (runit(w, gs) <= 0) {
		if (gs->critical > 0) {
			EMERG("Could not start monitor script %s, rebooting system ...", gs->script);
			if (checker_exec(gs->exec, "generic", 1, 100, gs->warning, gs->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		} else {
			WARN("Could not start monitor script %s, not critical.", gs->script);
		}
	}
}

static void cleanup(generic_t *gs)
{
	if (!gs)
		return;

	uev_timer_stop(&gs->script_watcher);
	uev_timer_stop(&gs->watcher);

	if (gs->exec)
		free(gs->exec);
	if (gs->script)
		free(gs->script);

	free(gs);
}

/*
 * Every T seconds we run the given script
 * If it returns nonzero or runs for more than timeout we are critical
 */
int generic_init(uev_ctx_t *ctx, int T, int timeout, char *monitor, int warn, int crit, char *script)
{
	static generic_t *gs = NULL;

	cleanup(gs);

	if (!T) {
		INFO("Generic script monitor disabled.");
		gs = NULL;
		return 0;
	}

	if (!monitor) {
		ERROR("Generic script monitor not started, please provide script-monitor.");
		gs = NULL;
		return 1;
	}

	INFO("Generic script monitor, period %d sec, max timeout: %d, "
	     "monitor script: %s, warning level: %d, critical level: %d",
	     T, timeout, monitor, warn, crit);

	gs = calloc(1, sizeof(generic_t));
	if (!gs) {
		PERROR("Failed initializing generic plugin");
		return 1;
	}

	gs->is_running = 0;
	gs->pid = -1;
	gs->warning = warn;
	gs->critical = crit;
	gs->script_runtime_max = timeout;
	gs->script = strdup(monitor);
	gs->exec = NULL;
	if (script)
		gs->exec = strdup(script);

	return uev_timer_init(ctx, &gs->watcher, cb, gs, T * 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
