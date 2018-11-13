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

typedef struct generic_script {
	uev_t  watcher;

	int    is_running;
	int    warning;
	int    critical;

	uev_t  monitor_script_watcher;
	int    monitor_run_time;
	int    max_monitor_run_time;
	char  *monitor_script;
	char  *exec;
	pid_t  pid;
} generic_script_t;


static void wait_for_generic_script(uev_t *w, void *arg, int events)
{
	generic_script_t *script = (generic_script_t *)arg;
	int status;

	DEBUG("Monitor Script (PID %d) verifying if still running, events: %d", script->pid, events);
	status = get_exit_code_for_pid(script->pid);
	if (status >= 0) {
		uev_timer_stop(&script->monitor_script_watcher);
		script->is_running = 0;

		if (status >= script->critical) {
			ERROR("Monitor Script (PID %d) returned exit status above critical treshold: %d, rebooting system ...", script->pid, status);
			if (checker_exec(script->exec, "generic", 1, status, script->warning, script->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);

			return;
		}

		if (status >= script->warning) {
			WARN("Monitor Script (PID %d) returned exit status above warning treshold: %d", script->pid, status);
			checker_exec(script->exec, "generic", 0, status, script->warning, script->critical);

			return;
		}

		INFO("Monitor Script (PID %d) ran OK", script->pid);
		return;
	}

	script->monitor_run_time += 1000;
	if (script->monitor_run_time >= script->max_monitor_run_time) {
		ERROR("Monitor Script (PID %d) still running after %d s", script->pid, script->max_monitor_run_time);
		if (checker_exec(script->exec, "generic", 1, 255, script->warning, script->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
	}
}

static int run_generic_script(uev_t *w, generic_script_t *script)
{
	pid_t pid;

	uev_timer_stop(&script->monitor_script_watcher);

	pid = generic_exec(script->monitor_script, script->warning, script->critical);
	if (pid <= 0) {
		ERROR("Could not start generic monitor script %s", script->monitor_script);
		return -1;
	}

	INFO("Started generic monitor script %s with PID %d", script->monitor_script, pid);

	script->pid = pid;
	script->is_running = 1;
	script->monitor_run_time = 0;

	uev_timer_init(w->ctx, &script->monitor_script_watcher, wait_for_generic_script, script, 1000, 1000);

	return script->pid;
}

static void cb(uev_t *w, void *arg, int events)
{
	generic_script_t *script = (generic_script_t *)arg;

	if (!script) {
		ERROR("Oops, no args?");
		return;
	}

	if (script->is_running) {
		ERROR("Timeout reached and the script %s is still running, rebooting system ...", script->monitor_script);
		if (checker_exec(script->exec, "generic", 1, 100, script->warning, script->critical))
			wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		return;
	}

	INFO("Starting the generic monitor script");

	if (run_generic_script(w, script) <= 0) {
		if (script->critical > 0) {
			ERROR("Could not start the monitor script %s, rebooting system ...", script->monitor_script);
			if (checker_exec(script->exec, "generic", 1, 100, script->warning, script->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
		} else {
			WARN("Could not start the monitor script %s, but is not critical", script->monitor_script);
		}
	}
}

static void stop_and_cleanup(generic_script_t *script)
{
	if (!script)
		return;

	uev_timer_stop(&script->monitor_script_watcher);
	uev_timer_stop(&script->watcher);

	if (script->exec)
		free(script->exec);
	if (script->monitor_script)
		free(script->monitor_script);

	free(script);
}

/*
 * Every T seconds we run the given script
 * If it returns nonzero or runs for more than timeout we are critical
 */
int generic_init(uev_ctx_t *ctx, int T, int timeout, char *monitor, int mark, int warn, int crit, char *script)
{
	static generic_script_t *gs = NULL;

	stop_and_cleanup(gs);

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

	gs = calloc(1, sizeof(generic_script_t));
	if (!gs) {
		PERROR("Failed initializing generic plugin");
		return 1;
	}

	gs->is_running = 0;
	gs->pid = -1;
	gs->warning = warn;
	gs->critical = crit;
	gs->max_monitor_run_time = timeout;
	gs->monitor_script = strdup(monitor);
	gs->exec = NULL;
	if (script)
		gs->exec = strdup(script);

	INFO("Starting generic script monitor timer ...");

	return uev_timer_init(ctx, &gs->watcher, cb, gs, T * 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
