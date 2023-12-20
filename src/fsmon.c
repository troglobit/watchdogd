/* File system monitor
 *
 * Copyright (C) 2015-2023  Joachim Wiberg <troglobit@gmail.com>
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

#include <sys/statvfs.h>
#include "wdt.h"
#include "script.h"

static const char *fsname = NULL;
static char *exec = NULL;
static int logmark = 0;
static uev_t watcher;

/* Default: disabled -- recommended 0.95, 1.0 */
static double warning  = 0.0;
static double critical = 0.0;


static void cb(uev_t *w, void *arg, int events)
{
	long unsigned int bused, fused;
	double blevel, flevel;
	struct statvfs f;

	if (statvfs(fsname, &f)) {
		PERROR("Failed statfs(%s)", fsname);
		return;
	}

	bused  = f.f_blocks - f.f_bavail;
	blevel = (double)(bused) / (double)(f.f_blocks);
	fused  = f.f_files - f.f_ffree;
	flevel = (double)(fused) / (double)(f.f_files);

	LOG("Fsmon %s: blocks %.0f%%, inodes %.0f%%, warning: %.0f%%, critical: %.0f%%",
	    fsname, blevel * 100, flevel * 100, warning * 100, critical * 100);
	if (logmark) {
		const char *ro = "(read-only";

		LOG("Fsmon %s: blocks %lu/%lu inodes %lu/%lu %s", fsname,
		    bused, f.f_bavail,
		    fused, f.f_ffree,
		    (f.f_flag & ST_RDONLY) ? ro : "");
	}

	if (blevel > warning || flevel > warning) {
		double level = blevel;

		if (flevel > blevel) {
			level = flevel;
			setenv("FSMON_TYPE", "inodes", 1);
		} else
			setenv("FSMON_TYPE", "blocks", 1);
		setenv("FSMON_NAME", fsname, 1);

		if (critical > 0.0 && (blevel > critical || flevel > warning)) {
			EMERG("File system %s usage too high, blocks %.2f > %0.2f, or inodes %.2f > %0.2f, rebooting system ...",
			      fsname, blevel, critical, flevel, critical);
			if (checker_exec(exec, "fsmon", 1, level, warning, critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":fsmon", 0);
		} else {
			WARN("File system %s use very high, blocks %.2f > %0.2f, inodes %.2f > %0.2f",
			     fsname, blevel, warning, flevel, warning);
			checker_exec(exec, "fsmon", 0, level, warning, critical);
		}

		unsetenv("FSMON_TYPE");
		unsetenv("FSMON_NAME");
	}
}

int fsmon_init(uev_ctx_t *ctx, const char *name, int T, int mark,
	       float warn, float crit, char *script)
{
	if (!name)
		name = "/";

	if (!T) {
		INFO("File descriptor leak monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	LOG("File system monitor: %s period %d sec, warning: %.2f%%, reboot: %.2f%%",
	     name, T, warning * 100, critical * 100);

	if (fsname)
		free(fsname);
	fsname = strdup(name);
	logmark = mark;
	warning = warn;
	critical = crit;
	if (script) {
		if (exec)
			free(exec);
		exec = strdup(script);
	}

	uev_timer_stop(&watcher);
	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
