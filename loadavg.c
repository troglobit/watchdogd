/* CPU load average monitor
 *
 * Copyright (C) 2015  Christian Lockley <clockley1@gmail.com>
 * Copyright (C) 2015  Joachim Nilsson <troglobit@gmail.com>
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

#include "wdt.h"

static uev_t watcher;

/* Default: disabled -- recommended 0.8, 0.9 */
static double warning  = 0;
static double critical = 0;


static double num_cores(void)
{
	int num = sysconf(_SC_NPROCESSORS_ONLN);

	if (-1 == num)
		return 1.0d;	/* At least one core. */

	return (double)num;
}

static void cb(uev_t *w, void *UNUSED(arg), int UNUSED(events))
{
	double num = num_cores();
	double avg, load[3];

	memset(load, 0, sizeof(load));
	if (getloadavg(load, 3) == -1) {
		ERROR("Failed reading system loadavg");
		return;
	}

	DEBUG("Load avg: %f, %f, %f (1, 5, 15 min) | Num CPU cores: %d",
	      load[0], load[1], load[2], (int)num);

	/* Compensate for number of CPU cores */
	load[0] /= num;
	load[1] /= num;
	load[2] /= num;
	avg = (load[0] + load[1]) / 2.0;

	DEBUG("Adjusted: %.2f, %.2f, %.2f (1, 5, 15 min), avg: %.2f (1 + 5), warning: %.2f, reboot: %.2f",
	      load[0], load[1], load[2], avg, warning, critical);

	if (avg > warning) {
		if (avg > critical) {
			ERROR("System load too high, rebooting system ...");
			wdt_reboot(w->ctx);
			return;
		}

		WARN("System load average very high!");
	}
}

/*
 * Every T seconds we check loadavg
 */
int loadavg_init(uev_ctx_t *ctx, int T)
{
	if (warning == 0.0 && critical == 0.0) {
		INFO("Load average monitoring disabled.");
		return 1;
	}

	INFO("Starting load average monitor, warning: %.2f, reboot: %.2f",
	     warning, critical);

	return uev_timer_init(ctx, &watcher, cb, NULL, T, T);
}

/*
 * Parse '-a warning[,critical]' argument
 */
int loadavg_set(char *arg)
{
	char buf[16], *ptr;
	double load;

	if (!arg) {
		ERROR("Load average argument missing.");
		return 1;
	}

	strlcpy(buf, arg, sizeof(buf));
	ptr = strchr(buf, ',');
	if (ptr) {
		*ptr++ = 0;
		DEBUG("Found second arg: %s", ptr);
	}

	/* First argument is warning */
	load = strtod(buf, NULL);
	if (load <= 0) {
	error:
		ERROR("Load average argument invalid or too small.");
		return 1;
	}
	warning = load;

	/* Second argument, if given, is warning */
	if (ptr) {
		load = strtod(ptr, NULL);
		if (load <= 0)
			goto error;
	} else {
		/* Backwards compat, when only warning is given */
		load += 0.1;
	}
	critical = load;

	DEBUG("Enabling loadavg check: %.2f, %.2f", warning, critical);

	return 0;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
