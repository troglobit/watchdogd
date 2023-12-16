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

#define MAX_EXEC_INFO_LIST_SIZE 50

struct exec_info {
	pid_t   pid;
	int     exit_status;
	void  (*cb)(void *arg);
	void   *arg;
	LIST_ENTRY(exec_info) entry;
};

static LIST_HEAD(exec_info_list, exec_info) exec_info_head;

static char *global_exec = NULL;
static uev_t watcher;


static void cleanup_exec_info(int max_size)
{
	struct exec_info *info, *next;
	int size = 0;

	LIST_FOREACH_SAFE(info, &exec_info_head, entry, next) {
		++size;
		if (size >= max_size) {
			LIST_REMOVE(info, entry);
			free(info);
		}
	}
}

static void add(pid_t pid, void (*cb)(void *), void *arg)
{
	struct exec_info *info;

	info = malloc(sizeof(*info));
	if (!info) {
		PERROR("Failed recording PID %d of script", pid);
		return;
	}

	info->pid = pid;
	info->cb  = cb;
	info->arg = arg;
	LIST_INSERT_HEAD(&exec_info_head, info, entry);
}

static int exec(pid_t pid, int status)
{
	struct exec_info *info;

	LIST_FOREACH(info, &exec_info_head, entry) {
		if (info->pid != pid)
			continue;

		info->exit_status = status;
		if (info->cb)
			info->cb(info->arg);

		return 0;
	}

	return -1;
}

int script_exit_status(pid_t pid)
{
	struct exec_info *info;
	int status = -1;

	LIST_FOREACH(info, &exec_info_head, entry) {
		if (info->pid != pid)
			continue;

		status = info->exit_status;
		LIST_REMOVE(info, entry);
		free(info);
		break;
	}

	return status;
}

static void cb(uev_t *w, void *arg, int events)
{
	pid_t pid = 1;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			if (status)
				WARN("Script (PID %d) returned error: %d", pid, status);
			else
				INFO("Script (PID %d) exited successful", pid);

			exec(pid, status);
			cleanup_exec_info(MAX_EXEC_INFO_LIST_SIZE - 1);
		} else {
			INFO("Script (PID %d) has not yet exited?", pid);
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

pid_t supervisor_exec(char *exec, int c, int p, char *label, void (*cb)(void *), void *arg)
{
	char cause[5], id[10];
	char *argv[] = {
		exec,
		"supervisor",
		NULL,
		NULL,
		label,
		NULL,
	};
	pid_t pid;

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
	add(pid, cb, arg);

	return pid;
}

pid_t generic_exec(char *exec, int warn, int crit)
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

	LOG("Started script %s, PID %d", exec, pid);
	add(pid, NULL, NULL);

	return pid;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
