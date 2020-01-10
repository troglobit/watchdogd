/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2018  Joachim Nilsson <troglobit@gmail.com>
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
#include "rr.h"
#include "supervisor.h"

static int fd = -1;
static char devnode[42] = WDT_DEVNODE;
static uev_t period_watcher;
static uev_t timeout_watcher;
static struct watchdog_info info;

/* Watchdogd reset reason as read at boot */
wdog_reason_t reset_reason;
wdog_code_t   reset_code    = WDOG_SYSTEM_OK;
unsigned int  reset_counter = 0;

/*
 * Connect to kernel wdt driver
 */
int wdt_open(const char *dev)
{
	static int once  = 0;

	if (fd >= 0)
		return 0;

	if (dev) {
		if (!strncmp(dev, "/dev", 4))
			strlcpy(devnode, dev, sizeof(devnode));
	}

	fd = open(devnode, O_WRONLY);
	if (fd == -1) {
#ifndef HAVE_FINIT_FINIT_H
		return -1;
#else
		if (EBUSY != errno)
			return -1;

		/*
		 * If we're called in a system with Finit running, tell it to
		 * disable its built-in watchdog daemon.
		 */
		fd = wdt_handover(devnode);
		if (fd == -1) {
			PERROR("Failed communicating WDT handover with finit");
			return -1;
		}

		wdt_kick("WDT handover complete.");
	} else {
		wdt_register();
#endif /* HAVE_FINIT_FINIT_H */
	}

	/* Skip capability check etc. if done already */
	if (once)
		return 0;

	/* For future calls due to SIGHUP, disable/enable, etc. */
	once = 1;

	/* Query WDT/driver capabilities */
	memset(&info, 0, sizeof(info));
	if (!ioctl(fd, WDIOC_GETSUPPORT, &info))
		INFO("%s: %s, capabilities 0x%04x", devnode, info.identity, info.options);

	/* Check capabilities */
	if (!wdt_capability(WDIOF_MAGICCLOSE)) {
		WARN("WDT cannot be disabled at runtime.");
		magic = 0;
	}

	if (!wdt_capability(WDIOF_POWERUNDER))
		WARN("WDT does not support PWR fail condition, treating as card reset.");

	return 0;
}

static void period_cb(uev_t *w, void *arg, int event)
{
	wdt_kick("Kicking watchdog.");
}

/*
 * Initialize, or reinitialize, connection to WDT.  Set timeout and
 * start a WDT kick timer.
 */
int wdt_init(uev_ctx_t *ctx, const char *dev)
{
	int T, err;

	if (wdt_testmode())
		return 0;

	err = wdt_open(dev);
	if (err)
		return 1;

	/* Set requested WDT timeout right before we enter the event loop. */
	if (wdt_set_timeout(timeout))
		PERROR("Failed setting HW watchdog timeout: %d", timeout);

	/* Sanity check with driver that setting actually took. */
	timeout = wdt_get_timeout();
	if (timeout < 0) {
		timeout = WDT_TIMEOUT_DEFAULT;
		PERROR("Failed reading current watchdog timeout");
	} else {
		if (timeout <= period) {
			ERROR("Warning, watchdog timeout <= kick interval: %d <= %d",
			      timeout, period);
		}
	}

	/* If user did not provide '-t' interval, set to half WDT timeout */
	if (-1 == period) {
		period = timeout / 2;
		if (!period)
			period = 1;
	}

	/* No ctx on re-enable at runtime */
	if (!ctx)
		return 0;

	/* Save/update /run/watchdogd.status */
	wdt_set_bootstatus(timeout, period);

	/* Calculate period (T) in milliseconds for libuEv */
	T = period * 1000;
	DEBUG("Watchdog kick interval set to %d sec.", period);

	/*
	 * On SIGHUP this stops the current kick before re-init.
	 * Otherwise this does nothing, libuEv takes care of us.
	 */
	uev_timer_stop(&period_watcher);

	/* Every period (T) seconds we kick the WDT */
	return uev_timer_init(ctx, &period_watcher, period_cb, NULL, T, T);
}

int wdt_capability(uint32_t flag)
{
	return (info.options & flag) == flag;
}

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
int wdt_kick(const char *msg)
{
	int dummy;

	DEBUG("%s", msg);
	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("No kick, currently disabled.");
		return 0;
	}

	if (!wdt_capability(WDIOF_CARDRESET))
		INFO("Kicking WDT.");

	return ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

/* FYI: The most common lowest setting is 120 sec. */
int wdt_set_timeout(int count)
{
	int arg = count;

	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("Cannot set timeout, currently disabled.");
		return 0;
	}

	if (!wdt_capability(WDIOF_SETTIMEOUT)) {
		WARN("WDT does not support setting timeout.");
		return 1;
	}

	DEBUG("Setting watchdog timeout to %d sec.", count);
	if (ioctl(fd, WDIOC_SETTIMEOUT, &arg))
		return 1;

	return 0;
}

int wdt_get_timeout(void)
{
	int count;
	int err;

	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("Cannot get timeout, currently disabled.");
		return 0;
	}

	err = ioctl(fd, WDIOC_GETTIMEOUT, &count);
	if (err)
		count = err;

	DEBUG("Watchdog timeout is set to %d sec.", count);

	return count;
}

int wdt_fload_reason(FILE *fp, wdog_reason_t *r, pid_t *pid)
{
	char *ptr, buf[80];
	pid_t dummy;

	if (!pid)
		pid = &dummy;

	while ((ptr = fgets(buf, sizeof(buf), fp))) {
		if (sscanf(buf, WDT_RESETCOUNT ": %u\n", &r->counter) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_PID ": %d\n", pid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_WID ": %d\n", &r->wid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_STR ": %d\n", (int *)&r->code) == 1)
			continue;

		if (string_match(buf, WDT_REASON_LBL ": ")) {
			ptr += sizeof(WDT_REASON_LBL) + 2;
			strlcpy(r->label, chomp(ptr), sizeof(r->label));
			continue;
		}

		if (string_match(buf, WDT_RESET_DATE ": ")) {
			ptr += sizeof(WDT_RESET_DATE) + 2;
			strptime(chomp(ptr), "%FT%TZ", &r->date);
			continue;
		}
	}

	return fclose(fp);
}

int wdt_fstore_reason(FILE *fp, wdog_reason_t *r, pid_t pid)
{
	time_t now;

	fprintf(fp, WDT_RESETCOUNT ": %u\n", r->counter);
	now = time(NULL);
	if (now != (time_t)-1) {
		char buf[25];

		strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));
		fprintf(fp, WDT_RESET_DATE ": %s\n", buf);
	}
	fprintf(fp, WDT_REASON_STR ": %d - %s\n", r->code, wdog_reset_reason_str(r));
	switch (r->code) {
	case WDOG_FAILED_SUBSCRIPTION:
	case WDOG_FAILED_KICK:
	case WDOG_FAILED_UNSUBSCRIPTION:
	case WDOG_FAILED_TO_MEET_DEADLINE:
		fprintf(fp, WDT_REASON_PID ": %d\n", pid);
		fprintf(fp, WDT_REASON_WID ": %d\n", r->wid);
		fprintf(fp, WDT_REASON_LBL ": %s\n", r->label);
		break;
	default:
		break;
	}

	return fclose(fp);
}

static int load_bootstatus(char *file, wdog_reason_t *r, pid_t *pid)
{
	FILE *fp;

	fp = fopen(file, "r");
	if (!fp)
		return -1;

	return wdt_fload_reason(fp, r, pid);
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

	fprintf(fp, "Watchdog ID  : %d\n", r->wid);
	fprintf(fp, "Label        : %s\n", r->label);
	fprintf(fp, "Reset cause  : %d (%s)\n", r->code, wdog_get_reason_str(r));
	fprintf(fp, "Counter      : %u\n", r->counter);

	return fclose(fp);
}
#else
#define compat_supervisor(r) 0
#endif /* COMPAT_SUPERVISOR */

const char *bootstatus_string(int cause)
{
	const char *str = NULL;

	if (cause & WDIOF_CARDRESET)
		str = "WDIOF_CARDRESET";
	if (cause & WDIOF_EXTERN1)
		str = "WDIOF_EXTERN1";
	if (cause & WDIOF_EXTERN2)
		str = "WDIOF_EXTERN2";
	if (cause & WDIOF_POWERUNDER)
		str = "WDIOF_POWERUNDER";
	if (cause & WDIOF_POWEROVER)
		str = "WDIOF_POWEROVER";
	if (cause & WDIOF_FANFAULT)
		str = "WDIOF_FANFAULT";
	if (cause & WDIOF_OVERHEAT)
		str = "WDIOF_OVERHEAT";

	if (!str)
		str = "WDIOF_UNKNOWN";

	return str;
}

static int create_bootstatus(char *fn, wdog_reason_t *r, int cause, int timeout, int interval, pid_t pid)
{
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		PERROR("Failed opening %s", WDOG_STATUS);
		return -1;
	}

	fprintf(fp, WDT_TMOSEC_OPT ": %d\n", timeout);
	fprintf(fp, WDT_INTSEC_OPT ": %d\n", interval);
	fprintf(fp, WDT_BOOTSTATUS ": 0x%04x\n", cause >= 0 ? cause : 0);
	fprintf(fp, WDT_RESETCAUSE ": %s\n", bootstatus_string(cause));

	 return wdt_fstore_reason(fp, r, pid);
}

int wdt_set_bootstatus(int timeout, int interval)
{
	wdog_reason_t reason;
	pid_t pid = 0;
	char *status;
	int cause;

	if (wdt_testmode())
		status = WDOG_STATUS_TEST;
	else
		status = WDOG_STATUS;

	cause = wdt_get_bootstatus();
	LOG("Reset cause: 0x%04x - %s", cause, bootstatus_string(cause));

	/*
	 * In case we're restarted at runtime this prevents us from
	 * recreating the status file(s).
	 */
	if (fexist(status)) {
		load_bootstatus(status, &reset_reason, &pid);
		reset_code   = reset_reason.code;
		reset_counter = reset_reason.counter;

		return create_bootstatus(status, &reset_reason, cause, timeout, interval, pid);
	}

	memset(&reason, 0, sizeof(reason));
	if (!reset_reason_get(&reason, &pid)) {
		reset_code   = reason.code;
		reset_counter = reason.counter;
	}

	/*
	 * Clear latest reset cause log IF and only IF WDT reports power
	 * failure as cause of this boot.  Keep reset counter, that must
	 * be reset using the API, snmpEngineBoots (RFC 2574)
	 */
	if (cause & WDIOF_POWERUNDER) {
		memset(&reason, 0, sizeof(reason));
		reason.counter = reset_counter;
		pid = 0;
	}

	if (!create_bootstatus(status, &reason, cause, timeout, interval, pid))
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

int wdt_get_bootstatus(void)
{
	int cause;
	int err;

	if (wdt_testmode())
		return 0;

	if (fd == -1) {
		DEBUG("Cannot get boot status, currently disabled.");
		return 0;
	}

	err = ioctl(fd, WDIOC_GETBOOTSTATUS, &cause);
	if (err)
		return err;

	return cause;
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
		/* Attempt to disable HW watchdog */
		while (fd != -1) {
			if (!wdt_capability(WDIOF_MAGICCLOSE)) {
				INFO("WDT cannot be disabled, continuing ...");
				break;
			}

			INFO("Attempting to disable HW watchdog timer.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now ...");

			close(fd);
			fd = -1;
		}
	} else {
		result += wdt_init(NULL, NULL);
	}

	return result;
}

int wdt_close(uev_ctx_t *ctx)
{
	/* Let supervisor exit before we leave main loop */
	supervisor_exit(ctx);

	if (fd != -1) {
		if (magic) {
			INFO("Disabling HW watchdog timer before (safe) exit.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now ...");
		} else {
			LOG("Exiting, watchdog still active.  Expect reboot!");
			/* Be nice, sync any buffered data to disk first. */
			sync();
		}

		close(fd);
	}

	/* Leave main loop. */
	return uev_exit(ctx);
}

int wdt_exit(uev_ctx_t *ctx)
{
	/* Let supervisor exit before we leave main loop */
	if (!rebooting)
		supervisor_exit(ctx);

	/* Be nice, sync any buffered data to disk first. */
	sync();

	if (fd != -1) {
		DEBUG("Forced watchdog reboot.");
		wdt_set_timeout(1);
		close(fd);
		fd = -1;
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
	wdt_exit(w->ctx);
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
#ifdef HAVE_FINIT_FINIT_H
	static int in_progress = 0;

	if (in_progress) {
		DEBUG("Reboot already in progress, due to supervisor/checker reset.");
		return 0;
	}
#endif

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

#ifdef HAVE_FINIT_FINIT_H
	if (!rebooting) {
		in_progress = 1;
		kill(1, SIGINT);
		timeout = 10000;
	}
#endif

	if (timeout > 0)
		return uev_timer_init(ctx, &timeout_watcher, reboot_timeout_cb, NULL, timeout, 0);

	return wdt_exit(ctx);
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
