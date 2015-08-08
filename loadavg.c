/* CPU load average functions
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

/* Default: disabled -- recommended 0.8, 0.9 */
double load_warn   = 0;
double load_reboot = 0;

/* Local variables */
static uev_t watcher;

static double num_cores(void)
{
	int num = sysconf(_SC_NPROCESSORS_ONLN);

	if (-1 == num)
		return 1.0d;	/* At least one core. */

	return (double)num;
}

static void cb(uev_t *w, void *arg, int events)
{
	double num = num_cores();
	double load[3];

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

	DEBUG("Adjusted: %f, %f, %f (1, 5, 15 min)", load[0], load[1], load[2]);

	if (load[1] > load_warn || load[0] > load_warn) {
		if (load[1] > load_reboot && load[0] > load_reboot) {
			ERROR("System load too high, rebooting system ...");
			wdt_reboot(w, arg, events);
		}

		WARN("System load average very high!");
	}
}

/* Every T seconds we check loadavg */
int loadavg_init(uev_ctx_t *ctx, int T)
{
	if (load_warn > 0.0 && load_reboot > 0.0) {
		INFO("Starting load average monitor, warn: %.2f, reboot: %.2f",
		     load_warn, load_reboot);
		return uev_timer_init(ctx, &watcher, cb, NULL, T, T);
	}

	INFO("Load average monitoring disabled.");

	return 1;
}

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

	/* First argument is load_warn */
	load = strtod(buf, NULL);
	if (load <= 0) {
	error:
		ERROR("Load average argument invalid or too small.");
		return 1;
	}
	load_warn = load;

	/* Second argument, if given, is load_warn */
	if (ptr) {
		load = strtod(ptr, NULL);
		if (load <= 0)
			goto error;
	} else {
		/* Backwards compat, when only warn is given */
		load += 0.1;
	}
	load_reboot = load;

	DEBUG("Enabling loadavg check: %.2f, %.2f", load_warn, load_reboot);

	return 0;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
