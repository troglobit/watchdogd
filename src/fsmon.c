/* File system monitor
 *
 * Copyright (C) 2015-2024  Joachim Wiberg <troglobit@gmail.com>
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

struct fsmon {
	TAILQ_ENTRY(fsmon) link; /* BSD sys/queue.h linked list node. */

	char  *name;
	char  *exec;
	int    logmark;

	float  warning;
	float  critical;

	uev_t  watcher;
	int    dirty;		/* for mark & sweep */
};

static TAILQ_HEAD(fshead, fsmon) fs = TAILQ_HEAD_INITIALIZER(fs);

static void cb(uev_t *w, void *arg, int events)
{
	struct fsmon *fs = (struct fsmon *)arg;
	long long unsigned int bused, fused;
	float blevel, flevel;
	struct statvfs f;

	if (statvfs(fs->name, &f)) {
		PERROR("Failed statfs(%s)", fs->name);
		return;
	}

	bused  = f.f_blocks - f.f_bavail;
	blevel = (float)bused / f.f_blocks;
	fused  = f.f_files - f.f_ffree;
	flevel = (float)(fused) / f.f_files;

//	LOG("Fsmon %s: blocks %.0f%%, inodes %.0f%%, warning: %.0f%%, critical: %.0f%%",
//	    fsname, blevel * 100, flevel * 100, warning * 100, critical * 100);
	if (fs->logmark) {
		const char *ro = "(read-only)";

		LOG("File system %s usage: blocks %llu/%llu inodes %llu/%llu %s",
		    fs->name, bused, (long long unsigned int)f.f_bavail,
		    fused, (long long unsigned int)f.f_ffree,
		    (f.f_flag & ST_RDONLY) ? ro : "");
	}

	if (blevel > fs->warning || flevel > fs->warning) {
		float level = blevel;

		if (flevel > blevel) {
			level = flevel;
			setenv("FSMON_TYPE", "inodes", 1);
		} else
			setenv("FSMON_TYPE", "blocks", 1);
		setenv("FSMON_NAME", fs->name, 1);

		if (fs->critical > 0.0 && level > fs->critical) {
			EMERG("File system %s usage too high, blocks %.2f > %0.2f, or inodes %.2f > %0.2f, rebooting system ...",
			      fs->name, blevel, fs->critical, flevel, fs->critical);
			if (checker_exec(fs->exec, "fsmon", 1, level, fs->warning, fs->critical))
				wdt_forced_reset(w->ctx, getpid(), PACKAGE ":fsmon", 0);
		} else {
			WARN("File system %s usage very high, blocks %.2f > %0.2f, inodes %.2f > %0.2f",
			     fs->name, blevel, fs->warning, flevel, fs->warning);
			checker_exec(fs->exec, "fsmon", 0, level, fs->warning, fs->critical);
		}

		unsetenv("FSMON_TYPE");
		unsetenv("FSMON_NAME");
	}
}

static struct fsmon *find(const char *name)
{
	struct fsmon *f;

	TAILQ_FOREACH(f, &fs, link) {
		if (strcmp(f->name, name))
			continue;

		return f;
	}

	return NULL;
}

void fsmon_mark(void)
{
	struct fsmon *f;

	TAILQ_FOREACH(f, &fs, link)
		f->dirty = 1;
}

void fsmon_sweep(void)
{
	struct fsmon *f, *tmp;

	TAILQ_FOREACH_SAFE(f, &fs, link, tmp) {
		if (!f->dirty)
			continue;

		TAILQ_REMOVE(&fs, f, link);
		uev_timer_stop(&f->watcher);
		free(f->name);
		free(f);
	}

	if (TAILQ_EMPTY(&fs)) {
		INFO("File system monitor disabled.");
	}
}

int fsmon_init(uev_ctx_t *ctx, const char *name, int T, int mark,
	       float warn, float crit, char *script)
{
	struct fsmon *f;

	if (!name) {
		if (T)
			ERROR("File system monitor missing path argument");
		return 1;
	}

	f = find(name);
	if (!f) {
		f = calloc(1, sizeof(*f));
		if (!f) {
		fail:
			PERROR("failed creating fsmon %s", name);
			return 1;
		}

		f->name = strdup(name);
		if (!f->name) {
			free(f);
			goto fail;
		}

		TAILQ_INSERT_TAIL(&fs, f, link);
	} else {
		f->dirty = 0;
		if (!T) {
			INFO("File system %s monitor disabled.", f->name);
			return uev_timer_stop(&f->watcher);
		}
	}


	INFO("File system %s monitor, period %d sec, warning: %.1f%%, reboot: %.1f%%",
	     name, T, warn * 100, crit * 100);

	f->logmark = mark;
	f->warning = warn;
	f->critical = crit;
	if (script) {
		if (f->exec)
			free(f->exec);
		f->exec = strdup(script);
	}

	uev_timer_stop(&f->watcher);

	return uev_timer_init(ctx, &f->watcher, cb, f, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
