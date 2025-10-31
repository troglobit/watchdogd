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

struct generic {
	TAILQ_ENTRY(generic) link; /* BSD sys/queue.h linked list node. */

	int    is_running;
	int    warning;
	int    critical;

	uev_t  script_watcher;
	int    script_runtime;
	int    script_runtime_max;
	char  *script;
	char  *exec;
	pid_t  pid;

	uev_t  watcher;
	int    dirty;		/* for mark & sweep */
};

static TAILQ_HEAD(gshead, generic) gs = TAILQ_HEAD_INITIALIZER(gs);

static void generic_cb(uev_t *w, void *arg, int events)
{
	struct generic *g = (struct generic *)arg;
	int status;

	DEBUG("Verifying if monitor script PID %d is still running", g->pid);
	status = script_exit_status(g->pid);
	if (status >= 0) {
		uev_timer_stop(&g->script_watcher);
		g->is_running = 0;

		if (status >= g->critical) {
			EMERG("Monitor script returned critical: %d, rebooting system ...", status);
			if (checker_exec(g->exec, "generic", 1, status, g->warning, g->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);

			return;
		}

		if (status >= g->warning) {
			WARN("Monitor script returned warning: %d", status);
			checker_exec(g->exec, "generic", 0, status, g->warning, g->critical);
		}

		return;
	}

	g->script_runtime += 1000;
	if (g->script_runtime >= (g->script_runtime_max * 1000)) {
		ERROR("Monitor script PID %d still running after %d sec", g->pid, g->script_runtime_max);
		if (checker_exec(g->exec, "generic", 1, 255, g->warning, g->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
	}
}

static int runit(uev_t *w, struct generic *g)
{
	pid_t pid;

	uev_timer_stop(&g->script_watcher);

	pid = generic_exec(g->script, g->warning, g->critical);
	if (pid <= 0) {
		ERROR("Could not start monitor script %s", g->script);
		return -1;
	}

	g->pid = pid;
	g->is_running = 1;
	g->script_runtime = 0;

	uev_timer_init(w->ctx, &g->script_watcher, generic_cb, g, 1000, 1000);

	return g->pid;
}

static void cb(uev_t *w, void *arg, int events)
{
	struct generic *g = (struct generic *)arg;

	if (!g)
		return;

	if (g->is_running) {
		EMERG("Timeout reached, script %s is still running, rebooting system ...", g->script);
		if (checker_exec(g->exec, "generic", 1, 100, g->warning, g->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		return;
	}

	if (runit(w, g) <= 0) {
		if (g->critical > 0) {
			EMERG("Could not start monitor script %s, rebooting system ...", g->script);
			if (checker_exec(g->exec, "generic", 1, 100, g->warning, g->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		} else {
			WARN("Could not start monitor script %s, not critical.", g->script);
		}
	}
}

static struct generic *find(const char *script)
{
	struct generic *g;

	TAILQ_FOREACH(g, &gs, link) {
		if (strcmp(g->script, script))
			continue;

		return g;
	}

	return NULL;
}

void generic_mark(void)
{
	struct generic *g;

	TAILQ_FOREACH(g, &gs, link) {
		g->dirty = 1;
	}
}

void generic_sweep(void)
{
	struct generic *g, *tmp;

	TAILQ_FOREACH_SAFE(g, &gs, link, tmp) {
		if (!g->dirty)
			continue;

		TAILQ_REMOVE(&gs, g, link);
		uev_timer_stop(&g->script_watcher);
		uev_timer_stop(&g->watcher);
		free(g->script);
		if (g->exec)
			free(g->exec);
		free(g);
	}
}

/*
 * Every T seconds we run the given script
 * If it returns nonzero or runs for more than timeout we are critical
 */
int generic_init(uev_ctx_t *ctx, const char *monitor, int T, int timeout, int warn, int crit, char *script)
{
	struct generic *g;

	g = find(monitor);
	if (!g) {
		g = calloc(1, sizeof(*g));
		if (!g) {
		fail:
			PERROR("Failed initializing generic plugin");
			return 1;
		}

		g->script = strdup(monitor);
		if (!g->script) {
			free(g);
			goto fail;
		}

		TAILQ_INSERT_TAIL(&gs, g, link);
	} else {
		g->dirty = 0;
		if (!T) {
			INFO("Generic script %s disabled.", g->script);
			g->is_running = 0;
			uev_timer_stop(&g->script_watcher);
			return uev_timer_stop(&g->watcher);
		}
	}

	INFO("Generic script monitor, period %d sec, max timeout: %d, "
	     "monitor script: %s, warning level: %d, critical level: %d",
	     T, timeout, monitor, warn, crit);

	g->is_running = 0;
	g->pid = -1;
	g->warning = warn;
	g->critical = crit;
	g->script_runtime_max = timeout;
	g->exec = NULL;
	if (script)
		g->exec = strdup(script);

	uev_timer_stop(&g->script_watcher);
	uev_timer_stop(&g->watcher);

	return uev_timer_init(ctx, &g->watcher, cb, g, T * 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
