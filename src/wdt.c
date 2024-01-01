/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2023  Joachim Wiberg <troglobit@gmail.com>
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

#include "finit.h"
#include "wdt.h"
#include "rr.h"
#include "supervisor.h"

/* Watchdogd reset reason as read at boot */
wdog_reason_t reset_reason;
wdog_code_t   reset_code    = WDOG_SYSTEM_OK;
unsigned int  reset_counter = 0;

static TAILQ_HEAD(devhead, wdt) devices = TAILQ_HEAD_INITIALIZER(devices);


static const char *wdt_flags(unsigned int cause, int json)
{
	static char buf[128];
	struct wdiof_xlate {
		unsigned int  flag;
		const char   *reason;
	} v[] = {
		/* reset cause and capability flags */
		{ WDIOF_OVERHEAT,   "overheat"    }, /* Reset due to CPU overheat         */
		{ WDIOF_FANFAULT,   "fan-fault"   }, /* Fan failed                        */
		{ WDIOF_EXTERN1,    "extern1"     }, /* External relay 1                  */
		{ WDIOF_EXTERN2,    "extern2"     }, /* External relay 2                  */

		{ WDIOF_POWERUNDER, "power-under" }, /* Power bad/power fault             */
		{ WDIOF_CARDRESET,  "card-reset"  }, /* Card previously reset the CPU     */
		{ WDIOF_POWEROVER,  "power-over"  }, /* Power over voltage                */
		/* capabilities-only flags below */
		{ WDIOF_SETTIMEOUT, "set-timeout" }, /* Set timeout (in seconds)          */

		{ WDIOF_MAGICCLOSE, "safe-exit"   }, /* Supports magic close char         */
		{ WDIOF_PRETIMEOUT, "pre-timeout" }, /* Pretimeout (in seconds), get/set  */
		{ WDIOF_ALARMONLY,  "alarm-only"  }, /* Watchdog triggers a management or
							other external alarm not a reboot */
		{ WDIOF_KEEPALIVEPING, "kick"     }, /* Keep alive ping reply (kick)      */
	};
	size_t i;

	buf[0] = 0;
	for (i = 0; i < NELEMS(v); i++) {
		if ((cause & v[i].flag) == v[i].flag) {
			cause &= ~v[i].flag;

			if (buf[0])
				strlcat(buf, ", ", sizeof(buf));
			if (json)
				strlcat(buf, "\"", sizeof(buf));
			strlcat(buf, v[i].reason, sizeof(buf));
			if (json)
				strlcat(buf, "\"", sizeof(buf));
		}
	}

	if (!buf[0])
		return json ? "\"unknown\"" : "unknown";

	return buf;
}

static struct wdt *find(const char *name)
{
	struct wdt *dev;

	TAILQ_FOREACH(dev, &devices, link) {
		if (strcmp(dev->name, name))
			continue;

		return dev;
	}

	return NULL;
}

void wdt_mark(void)
{
	struct wdt *dev;

	TAILQ_FOREACH(dev, &devices, link) {
		if (!dev->dirty)
			dev->dirty = 1;
	}
}

void wdt_sweep(void)
{
	struct wdt *dev, *tmp;

	TAILQ_FOREACH_SAFE(dev, &devices, link, tmp) {
		if (dev->dirty != 1)
			continue;

		TAILQ_REMOVE(&devices, dev, link);
		wdt_close(dev);
		free(dev->name);
		free(dev);
	}

	if (TAILQ_EMPTY(&devices))
		wdt_add(WDT_DEVNODE, period, timeout, magic, 1);

	/* update permanent/default from global settings */
	dev = TAILQ_FIRST(&devices);
	if (dev && dev->dirty == -1) {
		dev->timeout  = timeout;
		dev->interval = period;
		dev->magic    = magic;
	}
}

/*
 * Add device node to list of active watchdogs
 */
int wdt_add(const char *name, int interval, int timeout, int magic, int permanent)
{
	struct wdt *dev;

	if (!fexist(name)) {
		ERROR("Cannot find %s, skipping.", name);
		return 1;
	}

	dev = find(name);
	if (!dev) {
		dev = calloc(1, sizeof(*dev));
		if (!dev) {
		fail:
			PERROR("Failed adding watchdog %s", name);
			return 1;
		}

		dev->name = strdup(name);
		if (!dev->name) {
			free(dev);
			goto fail;
		}

		dev->fd = -1;
		if (permanent) {
			dev->dirty = -1;
			TAILQ_INSERT_HEAD(&devices, dev, link);
		} else
			TAILQ_INSERT_TAIL(&devices, dev, link);
	} else {
		dev->dirty = 0;
	}

	dev->interval = interval;
	dev->timeout  = timeout;
	dev->magic    = magic;

	return 0;
}

/*
 * Connect to kernel wdt driver
 */
int wdt_open(struct wdt *dev)
{
	if (dev->fd >= 0)
		return 0;

	dev->fd = open(dev->name, O_WRONLY);
	if (dev->fd == -1) {
		if (EBUSY != errno)
			return -1;

		/*
		 * If we're called in a system with Finit running, tell it to
		 * disable its built-in watchdog daemon.
		 */
		dev->fd = finit_handover(dev->name);
		if (dev->fd == -1) {
			if (errno != ENOENT)
				PERROR("Failed communicating WDT %s handover with finit", dev->name);
			return -1;
		}

		wdt_kick(dev, "WDT handover complete.");
	} else {
		finit_register(dev->name);
	}

	/* Query WDT/driver capabilities */
	memset(&dev->info, 0, sizeof(dev->info));
	if (!ioctl(dev->fd, WDIOC_GETSUPPORT, &dev->info))
		LOG("%s: %s, capabilities 0x%04x - %s", dev->name, dev->info.identity, dev->info.options, wdt_flags(dev->info.options, 0));

	if (!ioctl(dev->fd, WDIOC_GETBOOTSTATUS, &dev->reset_cause))
		LOG("%s: reset cause: 0x%04x - %s", dev->name, dev->reset_cause, wdt_flags(dev->reset_cause, 0));

	/* Check capabilities */
	if (!wdt_capability(dev, WDIOF_MAGICCLOSE)) {
		WARN("%s: cannot be disabled at runtime.", dev->name);
		dev->magic = 0;
	}

	if (!wdt_capability(dev, WDIOF_POWERUNDER))
		WARN("%s: does not support PWR fail condition, treating as card reset.", dev->name);

	return 0;
}

int wdt_close(struct wdt *dev)
{
	if (dev->fd != -1) {
		if (dev->magic) {
			INFO("%s: disarming watchdog timer before (safe) exit.", dev->name);
			if (-1 == write(dev->fd, "V", 1))
				PERROR("%s: failed disarming watchdog, system will likely reboot now ...", dev->name);
		} else {
			LOG("Exiting, watchdog %s still active.  Expect reboot!", dev->name);
			/* Be nice, sync any buffered data to disk first. */
			sync();
		}

		close(dev->fd);
		dev->fd = -1;
	}
	uev_timer_stop(&dev->watcher);

	return 0;
}

int wdt_capability(struct wdt *dev, uint32_t flag)
{
	return (dev->info.options & flag) == flag;
}

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
int wdt_kick(struct wdt *dev, const char *msg)
{
	int dummy;

	DEBUG("%s", msg);
	if (wdt_testmode())
		return 0;

	if (dev->fd == -1) {
		DEBUG("No kick, currently disabled.");
		return 0;
	}

	if (!wdt_capability(dev, WDIOF_CARDRESET))
		INFO("Kicking WDT %s", dev->name);

	return ioctl(dev->fd, WDIOC_KEEPALIVE, &dummy);
}

/* FYI: The most common lowest setting is 120 sec. */
int wdt_set_timeout(struct wdt *dev, int count)
{
	int arg = count;

	if (wdt_testmode())
		return 0;

	if (dev->fd == -1) {
		DEBUG("Cannot set timeout, currently disabled.");
		return 0;
	}

	if (!wdt_capability(dev, WDIOF_SETTIMEOUT)) {
		WARN("WDT %s does not support setting timeout.", dev->name);
		return 1;
	}

	DEBUG("Setting watchdog timeout to %d sec.", count);
	if (ioctl(dev->fd, WDIOC_SETTIMEOUT, &arg))
		return 1;

	return 0;
}

int wdt_get_timeout(struct wdt *dev)
{
	int count;
	int err;

	if (wdt_testmode())
		return 0;

	if (dev->fd == -1) {
		DEBUG("Cannot get timeout, currently disabled.");
		return 0;
	}

	err = ioctl(dev->fd, WDIOC_GETTIMEOUT, &count);
	if (err)
		count = err;

	DEBUG("Watchdog timeout is set to %d sec.", count);

	return count;
}

static int reset_was_powerloss(void)
{
	struct wdt *dev;

	TAILQ_FOREACH(dev, &devices, link) {
		if (dev->reset_cause & WDIOF_POWERUNDER)
			return 1;
	}

	return 0;
}

int wdt_fload_reason(FILE *fp, wdog_reason_t *r, pid_t *pid)
{
	char *ptr, buf[80];
	pid_t dummy;

	if (!pid)
		pid = &dummy;

	while ((ptr = fgets(buf, sizeof(buf), fp))) {
		chomp(buf);

		if (sscanf(buf, WDT_RESETCOUNT ": %u", &r->counter) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_PID ": %d", pid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_WID ": %u", &r->wid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_STR ": %d", (int *)&r->code) == 1)
			continue;

		if (string_match(buf, WDT_REASON_LBL)) {
			ptr = strchr(buf, ':');
			if (ptr) {
				if (strlen(ptr) > 2) {
					ptr += 2;
					strlcpy(r->label, ptr, sizeof(r->label));
				}
			}
			continue;
		}

		if (string_match(buf, WDT_RESET_DATE)) {
			ptr = strchr(buf, ':');
			if (ptr) {
				if (strlen(ptr) > 2) {
					ptr += 2;
					strptime(ptr, "%FT%TZ", &r->date);
				}
			}
			continue;
		}
	}

	return fclose(fp);
}

int wdt_fstore_reason(FILE *fp, wdog_reason_t *r, pid_t pid, int compat)
{
	char buf[25];
	time_t now;

	now = time(NULL);
	strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));

	if (compat) {
		/* Old compat format, still used for /var/lib/misc/watchdogd.state */
		fprintf(fp, WDT_RESETCOUNT ": %u\n", r->counter);
		fprintf(fp, WDT_RESET_DATE ": %s\n", buf);
		fprintf(fp, WDT_REASON_STR ": %d - %s\n", r->code, wdog_reset_reason_str(r));
		if (pid)
			fprintf(fp, WDT_REASON_PID ": %d\n", pid);

		switch (r->code) {
		case WDOG_FAILED_SUBSCRIPTION:
		case WDOG_FAILED_KICK:
		case WDOG_FAILED_UNSUBSCRIPTION:
		case WDOG_FAILED_TO_MEET_DEADLINE:
			fprintf(fp, WDT_REASON_WID ": %ui\n", r->wid);
			break;
		default:
			break;
		}

		if (r->label[0])
			fprintf(fp, WDT_REASON_LBL ": %s\n", r->label);
	} else {
		fprintf(fp, "  \"supervisor-reset\": {\n");
		fprintf(fp,
			"    \"code\": %d,\n"
			"    \"reason\": \"%s\",\n",
			r->code, wdog_reset_reason_str(r));
		switch (r->code) {
		case WDOG_FAILED_SUBSCRIPTION:
		case WDOG_FAILED_KICK:
		case WDOG_FAILED_UNSUBSCRIPTION:
		case WDOG_FAILED_TO_MEET_DEADLINE:
			fprintf(fp, "    \"watchdog-id\": %u,\n", r->wid);
			break;
		default:
			break;
		}
		if (pid)
			fprintf(fp, "    \"pid\": %d,\n", pid);
		if (r->label[0])
			fprintf(fp, "    \"label\": %s,\n", r->label);
		fprintf(fp,
			"    \"date\": \"%s\",\n"
			"    \"count\": %d\n  }\n}\n",
			buf, r->counter);
	}

	return fclose(fp);
}

#ifdef COMPAT_SUPERVISOR
static int compat_supervisor(wdog_reason_t *r)
{
	FILE *fp;

	/* Compat, created at boot from RTC contents */
	fp = fopen(_PATH_VARRUN "supervisor.status", "w");
        if (!fp) {
		PERROR("Failed creating compat boot status");
		return -1;
	}

	fprintf(fp, "Watchdog ID  : %u\n", r->wid);
	fprintf(fp, "Label        : %s\n", r->label);
	fprintf(fp, "Reset cause  : %d (%s)\n", r->code, wdog_get_reason_str(r));
	fprintf(fp, "Counter      : %u\n", r->counter);

	return fclose(fp);
}
#else
#define compat_supervisor(r) 0
#endif /* COMPAT_SUPERVISOR */

static int create_bootstatus(char *fn, wdog_reason_t *r, pid_t pid)
{
	struct wdt *dev;
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		PERROR("Failed opening %s", WDOG_STATUS);
		return -1;
	}

	fprintf(fp, "{\n  \"device\": [\n");
	TAILQ_FOREACH(dev, &devices, link) {
		fprintf(fp, "    {\n");
		fprintf(fp, "      \"name\": \"%s\",\n", dev->name);
		fprintf(fp, "      \"identity\": \"%s\",\n", dev->info.identity);
		fprintf(fp, "      \"fw-version\": %u,\n", dev->info.firmware_version);
		fprintf(fp, "      \"timeout\": %d,\n", dev->timeout);
		fprintf(fp, "      \"interval\": %d,\n", dev->interval);
		fprintf(fp, "      \"capabilities\": {\n");
		fprintf(fp, "        \"mask\":\"0x%04x\",\n", dev->info.options);
		fprintf(fp, "        \"flags\": [ %s ]\n", wdt_flags(dev->info.options, 1));
		fprintf(fp, "      },\n");
		fprintf(fp, "      \"reset-cause\": {\n");
		fprintf(fp, "        \"cause\": \"0x%04x\",\n", dev->reset_cause);
		fprintf(fp, "        \"flags\": [ %s ]\n", wdt_flags(dev->reset_cause, 1));
		fprintf(fp, "      }\n");
		fprintf(fp, "    }%s\n", TAILQ_NEXT(dev, link) ? "," : "");
	}
	fprintf(fp, "  ],\n");

	return wdt_fstore_reason(fp, r, pid, 0);
}

static int save_bootstatus(void)
{
	wdog_reason_t reason;
	pid_t pid = 0;
	char *status;

	if (wdt_testmode())
		status = WDOG_STATUS_TEST;
	else
		status = WDOG_STATUS;

	/*
	 * In case we're restarted at runtime this prevents us from
	 * recreating the status file(s).
	 */
	if (fexist(status))
		return 0;

	memset(&reason, 0, sizeof(reason));
	if (!reset_reason_get(&reason, &pid)) {
		reset_code    = reason.code;
		reset_counter = reason.counter;
	}

	/*
	 * Clear latest reset cause and counter IF and only IF the WDT
	 * reports power failure as cause of this boot.
	 */
	if (reset_was_powerloss()) {
		memset(&reason, 0, sizeof(reason));
		reset_counter = 0;
		pid = 0;
	}

	if (!create_bootstatus(status, &reason, pid))
		memcpy(&reset_reason, &reason, sizeof(reset_reason));

	/*
	 * Prepare for power-loss or otherwise uncontrolled reset
	 * The operator expects us to track the n:o restarts ...
	 */
	memset(&reason, 0, sizeof(reason));
	reason.code   = WDOG_FAILED_UNKNOWN;
	reason.counter = reset_counter + 1;
	reset_reason_clear(&reason);

	if (wdt_testmode())
		return 0;

	return compat_supervisor(&reset_reason);
}

int wdt_enable(int enable)
{
	int result = 0;

	if (enabled == enable)
		return 0;	/* Hello?  Yes, this is dog */

	/* Stop/Start process supervisor */
	DEBUG("%sabling supervisor ...", enable ? "En" : "Dis");
	result += supervisor_enable(enable);
	if (!result)
		enabled = enable;

	DEBUG("%sabling watchdogd ...", enable ? "En" : "Dis");
	if (!enable) {
		struct wdt *dev;

		/* Attempt to disable HW watchdogs */
		TAILQ_FOREACH(dev, &devices, link) {
			if (dev->fd == -1)
				continue;

			if (!wdt_capability(dev, WDIOF_MAGICCLOSE)) {
				INFO("WDT cannot be disabled, continuing ...");
				continue;
			}

			INFO("Attempting to disable HW watchdog timer.");
			if (-1 == write(dev->fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now ...");

			close(dev->fd);
			dev->fd = -1;
		}
	} else {
		result += wdt_init(NULL, NULL);
	}

	return result;
}


static void period_cb(uev_t *w, void *arg, int event)
{
	wdt_kick(arg, "Kicking watchdog.");
}

/*
 * Initialize, or reinitialize, connection to WDT.  Set timeout and
 * start a WDT kick timer.
 */
int wdt_init(uev_ctx_t *ctx, const char *devnode)
{
	struct wdt *dev;

	if (wdt_testmode())
		return 0;

	if (devnode) {
		/* Check if already in .conf file */
		dev = find(devnode);
		if (!dev)
			wdt_add(devnode, period, timeout, magic, 1);
	}

	TAILQ_FOREACH(dev, &devices, link) {
		int T, err, tmo;

		err = wdt_open(dev);
		if (err)
			continue;

		if (ctx)
			dev->ctx = ctx;

		/* Set requested WDT timeout right before we enter the event loop. */
		if (wdt_set_timeout(dev, dev->timeout))
			PERROR("Failed setting watchdog %s timeout: %d", dev->name, dev->timeout);

		/* Sanity check with driver that setting actually took. */
		tmo = wdt_get_timeout(dev);
		if (tmo < 0) {
			dev->timeout = WDT_TIMEOUT_DEFAULT;
			PERROR("Failed checking watchdog %s timeout, guessing default %d", dev->name, dev->timeout);
		} else {
			if (tmo != dev->timeout) {
				ERROR("Watchdog %s timeout is %d, adjusting your setting.", dev->name, tmo);
				dev->timeout = tmo;
			}

			if (tmo <= dev->interval) {
				ERROR("Warning, watchdog %s timeout <= kick interval: %d <= %d", dev->name,
				      tmo, dev->interval);
			}
		}

		/* If user did not provide '-t' interval, set to half WDT timeout */
		if (!dev->interval) {
			dev->interval = dev->timeout / 2;
			if (!dev->interval)
				dev->interval = 1;
		}

		/* Calculate period (T) in milliseconds for libuEv */
		T = dev->interval * 1000;
		DEBUG("Watchdog device %s kick interval set to %d sec.", dev->name, dev->interval);

		/*
		 * On SIGHUP this stops the current kick before re-init.
		 * Otherwise this does nothing, libuEv takes care of us.
		 */
		uev_timer_stop(&dev->watcher);

		/* Every period (T) seconds we kick the WDT */
		uev_timer_init(dev->ctx, &dev->watcher, period_cb, dev, T, T);
	}

	/* Save/update /run/watchdogd/status */
	return save_bootstatus();
}

int wdt_exit(uev_ctx_t *ctx)
{
	struct wdt *dev, *tmp;

	/* Let supervisor exit before we leave main loop */
	supervisor_exit(ctx);

	TAILQ_FOREACH_SAFE(dev, &devices, link, tmp) {
		TAILQ_REMOVE(&devices, dev, link);
		wdt_close(dev);
		free(dev->name);
		free(dev);
	}

	/* Leave main loop. */
	return uev_exit(ctx);
}

int wdt_reboot(uev_ctx_t *ctx)
{
	struct wdt *dev;

	/* Let supervisor exit before we leave main loop */
	if (!rebooting)
		supervisor_exit(ctx);

	/* Be nice, sync any buffered data to disk first. */
	sync();

	TAILQ_FOREACH(dev, &devices, link) {
		if (dev->fd != -1) {
			DEBUG("Forced watchdog %s reboot.", dev->name);
			wdt_set_timeout(dev, 1);
			close(dev->fd);
			dev->fd = -1;
		}
	}

	/*
	 * Tell main() to loop until the HW WDT reboots us ... but only
	 * if we're still enabled.  If we've been disabled prior to our
	 * being called to exit, we should just exit immediately.
	 */
	wait_reboot = enabled;

	/* Leave main loop. */
	return uev_exit(ctx);
}

/*
 * Callback for timed reboot
 */
static void reboot_timeout_cb(uev_t *w, void *arg, int events)
{
	wdt_reboot(w->ctx);
}

/*
 * Exit and reboot system -- reason for reboot is stored in some form of
 * semi-persistent backend, using @pid and @label, defined at compile
 * time.  By default the backend will be a regular file in /var/lib/,
 * most likely /var/lib/misc/watchdogd.state -- see the FHS for details
 * http://www.pathname.com/fhs/pub/fhs-2.3.html#VARLIBVARIABLESTATEINFORMATION
 */
int wdt_reset(uev_ctx_t *ctx, pid_t pid, wdog_reason_t *reason, int timeout)
{
	static uev_t timeout_watcher;
	static int in_progress = 0;

	if (in_progress && is_finit_system()) {
		DEBUG("Reboot already in progress, due to supervisor/checker reset.");
		return 0;
	}

	if (!ctx || !reason)
		return errno = EINVAL;

	if (!pid)
		DEBUG("Reboot from command line, label %s, timeout: %d ...", reason->label, timeout);
	else
		DEBUG("Reboot requested by pid %d, label %s, timeout: %d ...", pid, reason->label, timeout);

	/* Save reset cause */
	reason->counter = reset_counter + 1;
	reset_reason_set(reason, pid);

	/* Only save reset cause, no reboot ... */
	if (timeout < 0)
		return 0;

	if (!rebooting && is_finit_system()) {
		in_progress = 1;
		kill(1, SIGTERM);
		timeout = 10000;
	}

	if (timeout > 0)
		return uev_timer_init(ctx, &timeout_watcher, reboot_timeout_cb, NULL, timeout, 0);

	return wdt_reboot(ctx);
}

/* timeout is in milliseconds */
int wdt_forced_reset(uev_ctx_t *ctx, pid_t pid, char *label, int timeout)
{
	wdog_reason_t reason;

	memset(&reason, 0, sizeof(reason));
	reason.code = WDOG_FORCED_RESET;
	strlcpy(reason.label, label, sizeof(reason.label));

	return wdt_reset(ctx, pid, &reason, timeout);
}


/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
