/* Daemonize by double-forking and detaching from the session context.
 *
 * Copyright (C) 2003,2004,2008  Joachim Nilsson <troglobit@gmail.com>
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

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * daemonize - Fork off the deamon process and detach from session context.
 * @output: File to redirect stderr and stdout to.
 *
 * Disassociate from process group and controlling terminal forking off
 * the daemon process. The function returns a fork() pid value as result.
 *
 * It is customary to honor SIGTERM and SIGHUP in daemons, so remember
 * to setup the appropriate sighandlers for them.
 *
 * If @output is not %NULL then stderr and stdout are redirected to the
 * given file.  If that file cannot be created output is lost, hence the
 * program will continue, but witout any output.
 *
 * Returns:
 * A pid value. Negative value is error, positive is child pid, zero
 * indicates that the child is executing.
 */
int daemonize(char *output)
{
	int fd;
	pid_t pid;

	/* Fork off daemon process */
	pid = fork();
	if (0 != pid) {
		if (pid > 0)
			/* Harvest the offspring. */
			wait(NULL);

		return pid;
	}

	/* or setsid() to lose control terminal and change process group */
	setpgid(0, 0);

	/* Prevent reacquisition of a controlling terminal */
	pid = fork();
	if (0 != pid)
		/* Reap the child and let the grandson live on. */
		exit(EXIT_SUCCESS);

	/* If parent is NOT init. */
	if (1 != getppid()) {
		/* Ignore if background tty attempts write. */
		signal(SIGTTOU, SIG_IGN);
		/* Ignore if background tty attempts read. */
		signal(SIGTTIN, SIG_IGN);
		/* Ignore any keyboard generated stop signal signals. */
		signal(SIGTSTP, SIG_IGN);

		/* Become session leader and group process leader with no
		 * controlling terminal */
		setsid();
	}

	/* Redirect standard files */
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		close(fd);
	} else {
		close(STDIN_FILENO);
	}

	if (output && (fd = open(output, O_RDWR | O_CREAT | O_TRUNC, 0644)) != -1) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
	} else {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}

	/* Move current directory off mounted file system */
	chdir("/");

	/* Clear any inherited file mode creation mask */
	umask(0);

	return pid;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
