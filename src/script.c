/* Run script as monitor plugin callback
 *
 * Copyright (c) 2017  Joachim Nilsson <troglobit@gmail.com>
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
#include <stdlib.h>		/* setenv() */
#include <sys/wait.h>		/* waitpid() */
#include <unistd.h>		/* execv(), _exit() */

#include "wdt.h"
#include "script.h"

static char *global_exec = NULL;
static uev_t watcher;

static void cb(uev_t *w, void *arg, int events)
{
	int status;
	pid_t pid = 1;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		/* Script exit OK. */
		if (WIFEXITED(status))
			continue;

		/* Script exit status ... */
		status = WEXITSTATUS(status);
		if (status)
			WARN("Script (PID %d) returned error: %d", pid, status);
	}
}

int script_init(uev_ctx_t *ctx, char *script)
{
	static int once = 1;

	if (script && access(script, X_OK)) {
		ERROR("%s is not executable.", script);
		return -1;
	}

	if (global_exec)
		free(global_exec);

	if (script)
		global_exec = strdup(script);
	else
		global_exec = NULL;

	/* Only set up signal watcher once */
	if (once) {
		once = 0;
		uev_signal_init(ctx, &watcher, cb, "init", SIGCHLD);
	}

	return 0;
}

int script_exec(char *exec, char *nm, int iscrit, double val, double warn, double crit)
{
	pid_t pid;
	char warning[5], critical[5], value[5];
	char *argv[] = {
		exec,
		nm,
		iscrit ? "crit" : "warn",
		NULL,
		NULL,
	};

	/* Fall back to global setting checker lacks own script */
	if (!exec) {
		if (!global_exec)
			return 1;
		argv[0] = global_exec;
	}

	snprintf(value, sizeof(value), "%.2f", val);
	argv[3] = value;

	snprintf(warning, sizeof(warning), "%.2f", warn);
	snprintf(critical, sizeof(critical), "%.2f", crit);
	setenv("WARN", warning, 1);
	setenv("CRIT", critical, 1);

	pid = fork();
	if (!pid)
		_exit(execv(argv[0], argv));
	if (pid < 0) {
		PERROR("Cannot start script %s", exec);
		return -1;
	}
	LOG("Started script %s, PID %d", exec, pid);

	return 0;
}

int supervisor_script_exec(char *exec, char *label, pid_t proc_pid)
{
	pid_t pid;
	char value[10];
	char *argv[] = {
		exec,
		"supervisor",
		label,
		NULL,
		NULL
	};

	/* Fall back to global setting checker lacks own script */
	if (!exec)
		return 1;

	snprintf(value, sizeof(value), "%u", proc_pid);
	argv[3] = value;

	pid = fork();
	if (!pid)
		_exit(execv(argv[0], argv));
	if (pid < 0) {
		PERROR("Cannot start script %s", exec);
		return -1;
	}
	LOG("Started script %s, PID %d", exec, pid);

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */

