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
#include <signal.h>		/* sigemptyset(), sigaction() */
#include <sys/wait.h>		/* waitpid() */
#include <unistd.h>		/* execv(), _exit() */

#include "wdt.h"
#include "script.h"

static char *exec   = NULL;
static pid_t script = 0;

static void handler(int signo)
{
	int status;
	pid_t pid = 1;

	while (pid > 0) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid == script) {
			script = 0;

			/* Script exit OK. */
			if (WIFEXITED(status))
				continue;

			/* Script exit status ... */
			status = WEXITSTATUS(status);
			if (status)
				WARN("Script %s returned error: %d", exec, status);
		}
	}
}

int script_init(char *script)
{
	struct sigaction sa;

	if (script && access(script, X_OK)) {
		ERROR("%s is not executable.", script);
		return -1;
	}
	exec = script;

	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	return 0;
}

int script_exec(char *nm, int iscrit, double val, double warn, double crit)
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

	if (!exec)
		return 1;

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

	script = pid;
	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */

