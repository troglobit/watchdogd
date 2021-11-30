/* Run script as monitor plugin callback
 *
 * Copyright (c) 2017-2020  Joachim Wiberg <troglobit@gmail.com>
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

#ifdef _LIBITE_LITE
# include <libite/queue.h>
#else
# include <lite/queue.h>
#endif

#include "wdt.h"
#include "script.h"

#define MAX_EXEC_INFO_LIST_SIZE 5

static char *global_exec = NULL;
static uev_t watcher;
typedef struct exec_info {
	pid_t pid;
	int exit_status;
	LIST_ENTRY(exec_info) entry;
} exec_info_t;
static LIST_HEAD(exec_info_list, exec_info) exec_info_head;

static void cleanup_exec_info(int max_size)
{
	exec_info_t *info, *next;
	int size = 0;

	LIST_FOREACH_SAFE(info, &exec_info_head, entry, next) {
		++size;
		if (size >= max_size) {
			LIST_REMOVE(info, entry);
			free(info);
		}
	}
}

static void cb(uev_t *w, void *arg, int events)
{
	exec_info_t *info;
	pid_t pid = 1;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			if (status)
				WARN("Script (PID %d) returned error: %d", pid, status);
			else
				INFO("Script (PID %d) exited successful", pid);

			cleanup_exec_info(MAX_EXEC_INFO_LIST_SIZE - 1);

			info = malloc(sizeof(*info));
			if (!info) {
				PERROR("Failed recording PID %d exit status %d", pid, status);
				return;
			}

			info->pid = pid;
			info->exit_status = status;
			LIST_INSERT_HEAD(&exec_info_head, info, entry);
		} else {
			INFO("Script (PID %d) is not yet exited?", pid);
		}
	}
}

int script_init(uev_ctx_t *ctx, char *script)
{
	static int once = 1;

	/* Only set up signal watcher once */
	if (once) {
		once = 0;
		LIST_INIT(&exec_info_head);
		uev_signal_init(ctx, &watcher, cb, "init", SIGCHLD);
	}

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

	return 0;
}

int checker_exec(char *exec, char *nm, int iscrit, double val, double warn, double crit)
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

int supervisor_exec(char *exec, int c, int p, char *label)
{
	pid_t pid;
	char cause[5], id[10];
	char *argv[] = {
		exec,
		"supervisor",
		NULL,
		NULL,
		label,
		NULL,
	};

	snprintf(cause, sizeof(cause), "%d", c);
	argv[2] = cause;

	snprintf(id, sizeof(id), "%d", p);
	argv[3] = id;

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

int generic_exec(char *exec, int warn, int crit)
{
	pid_t pid;
	char value[5];
	char *argv[] = {
		exec,
		NULL,
		NULL,
		0
	};

	snprintf(value, sizeof(value), "%d", warn);
	argv[1] = value;
	snprintf(value, sizeof(value), "%d", crit);
	argv[2] = value;

	pid = fork();
	if (!pid)
		_exit(execv(argv[0], argv));
	if (pid < 0) {
		PERROR("Cannot start script %s", exec);
		return -1;
	}
	INFO("Started generic script %s, PID %d", exec, pid);

	return pid;
}

int exit_code(pid_t pid)
{
	exec_info_t *info;
	int rc = -1;

	LIST_FOREACH(info, &exec_info_head, entry) {
		if (info->pid != pid)
			continue;

		rc = info->exit_status;
		LIST_REMOVE(info, entry);
		free(info);
		break;
	}

	return rc;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
