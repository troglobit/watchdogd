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
#include "plugin.h"
#include "rc.h"

#ifndef HAVE_FINIT_FINIT_H
#define check_handover(devnode)						\
	return 1;
#else
#define check_handover(devnode)						\
{									\
	if (EBUSY != errno)						\
		return 1;						\
									\
	/*								\
	 * If we're called in a system with Finit running, tell it to	\
	 * disable its built-in watchdog daemon.			\
	 */								\
	fd = wdt_handover(devnode);					\
	if (fd == -1) {							\
		PERROR("Failed communicating WDT handover with finit");	\
		return 1;						\
	}								\
									\
	wdt_kick("WDT handover complete.");				\
}
#endif

static int fd = -1;
static char devnode[42] = WDT_DEVNODE;
static uev_t timeout_watcher;
static struct watchdog_info info;

/* Actual reboot reason as read at boot, reported by supervisor API */
wdog_reason_t reboot_reason;

/* Reset cause */
wdog_cause_t reset_cause   = WDOG_SYSTEM_OK;
unsigned int reset_counter = 0;


/*
 * Connect to kernel wdt driver
 */
int wdt_init(char *dev)
{
	if (wdt_testmode())
		return 0;

	if (dev) {
		if (!strncmp(dev, "/dev", 4))
			strlcpy(devnode, dev, sizeof(devnode));
	}

	fd = open(devnode, O_WRONLY);
	if (fd == -1)
		check_handover(devnode);

	memset(&info, 0, sizeof(info));
	if (!ioctl(fd, WDIOC_GETSUPPORT, &info))
		INFO("%s: %s, capabilities 0x%04x", devnode, info.identity, info.options);

	return 0;
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
int wdt_kick(char *msg)
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

	DEBUG("Previous timeout was %d sec", arg);

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
		if (sscanf(buf, WDT_REASON_CNT ": %u\n", &r->counter) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_PID ": %d\n", pid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_WID ": %d\n", &r->wid) == 1)
			continue;
		if (sscanf(buf, WDT_REASON_CSE ": %d\n", (int *)&r->cause) == 1)
			continue;

		if (string_match(buf, WDT_REASON_LBL ": ")) {
			ptr += strlen(WDT_REASON_LBL) + 2;
			strlcpy(r->label, chomp(ptr), sizeof(r->label));
			continue;
		}
	}

	return fclose(fp);
}

int wdt_fstore_reason(FILE *fp, wdog_reason_t *r, pid_t pid)
{
	fprintf(fp, WDT_REASON_CNT ": %u\n", r->counter);
	fprintf(fp, WDT_REASON_PID ": %d\n", pid);
	fprintf(fp, WDT_REASON_WID ": %d\n", r->wid);
	fprintf(fp, WDT_REASON_LBL ": %s\n", r->label);
	fprintf(fp, WDT_REASON_CSE ": %d\n", r->cause);
	fprintf(fp, WDT_REASON_STR ": %s\n", wdog_reboot_reason_str(r));

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
	fprintf(fp, "Reset cause  : %d (%s)\n", r->cause, wdog_get_reason_str(r));
	fprintf(fp, "Counter      : %u\n", r->counter);

	return fclose(fp);
}
#else
#define compat_supervisor(r) 0
#endif /* COMPAT_SUPERVISOR */

static int create_bootstatus(char *fn, wdog_reason_t *r, int cause, int timeout, int interval, pid_t pid)
{
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		PERROR("Failed opening %s", WDOG_STATUS);
		return -1;
	}

	fprintf(fp, WDT_REASON_WDT ": 0x%04x\n", cause >= 0 ? cause : 0);
	fprintf(fp, WDT_REASON_TMO ": %d\n", timeout);
	fprintf(fp, WDT_REASON_INT ": %d\n", interval);

	 return wdt_fstore_reason(fp, r, pid);
}

int wdt_set_bootstatus(int cause, int timeout, int interval)
{
	pid_t pid = 0;
	char *status;
	wdog_reason_t reason;

	if (wdt_testmode())
		status = WDOG_STATUS_TEST;
	else
		status = WDOG_STATUS;

	/*
	 * In case we're restarted at runtime this prevents us from
	 * recreating the status file(s).
	 */
	if (fexist(status)) {
		load_bootstatus(status, &reboot_reason, &pid);
		reset_cause   = reboot_reason.cause;
		reset_counter = reboot_reason.counter;

		return create_bootstatus(status, &reboot_reason, cause, timeout, interval, pid);
	}

	memset(&reason, 0, sizeof(reason));
	if (!reset_cause_get(&reason, &pid)) {
		reset_cause   = reason.cause;
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
		memcpy(&reboot_reason, &reason, sizeof(reboot_reason));

	/*
	 * Prepare for power-loss or otherwise uncontrolled reset
	 * The operator expects us to track the n:o restarts ...
	 */
	memset(&reason, 0, sizeof(reason));
	reason.cause   = WDOG_FAILED_UNKNOWN;
	reason.counter = reset_counter + 1;
	reset_cause_clear(&reason);

	if (wdt_testmode())
		return 0;

	return compat_supervisor(&reboot_reason);
}

int wdt_get_bootstatus(void)
{
	int status = 0;
	int err;

	if (wdt_testmode())
		return status;

	if (fd == -1) {
		DEBUG("Cannot get boot status, currently disabled.");
		return 0;
	}

	if ((err = ioctl(fd, WDIOC_GETBOOTSTATUS, &status)))
		status += err;

	if (!err && status) {
		if (status & WDIOF_POWERUNDER)
			LOG("Reset cause: POWER-ON");
		if (status & WDIOF_FANFAULT)
			LOG("Reset cause: FAN-FAULT");
		if (status & WDIOF_OVERHEAT)
			LOG("Reset cause: CPU-OVERHEAT");
		if (status & WDIOF_CARDRESET)
			LOG("Reset cause: WATCHDOG");
	}

	return status;
}

int wdt_enable(int enable)
{
	int result = 0;

	if (enabled == enable)
		return 0;	/* Hello?  Yes, this is dog */

	if (!enable) {
		/* Attempt to disable HW watchdog */
		if (fd != -1) {
			if (!wdt_capability(WDIOF_MAGICCLOSE)) {
				ERROR("WDT cannot be disabled, aborting operation.");
				return 1;
			}

			INFO("Attempting to disable HW watchdog timer.");
			if (-1 == write(fd, "V", 1))
				PERROR("Failed disabling HW watchdog, system will likely reboot now");

			close(fd);
			fd = -1;
		}
	} else {
		result += wdt_init(NULL);
	}

	/* Stop/Start process supervisor */
	result += supervisor_enable(enable);
	if (!result)
		enabled = enable;

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
				PERROR("Failed disabling HW watchdog before exit, system will likely reboot now");
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

	/* Tell main() to loop until reboot ... */
	wait_reboot = 1;

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
int wdt_reboot(uev_ctx_t *ctx, pid_t pid, wdog_reason_t *reason, int timeout)
{
	if (!ctx || !reason)
		return errno = EINVAL;

	if (!pid)
		DEBUG("Reboot from command line, label %s, timeout: %d ...", reason->label, timeout);
	else
		DEBUG("Reboot requested by pid %d, label %s, timeout: %d ...", pid, reason->label, timeout);

	/* Save reboot cause */
	reason->counter = reset_counter + 1;
	reset_cause_set(reason, pid);

	if (timeout > 0)
		return uev_timer_init(ctx, &timeout_watcher, reboot_timeout_cb, NULL, timeout, 0);

	return wdt_exit(ctx);
}

/* timeout is in milliseconds */
int wdt_forced_reboot(uev_ctx_t *ctx, pid_t pid, char *label, int timeout)
{
	wdog_reason_t reason;

	memset(&reason, 0, sizeof(reason));
	reason.cause = WDOG_FORCED_RESET;
	strlcpy(reason.label, label, sizeof(reason.label));

	return wdt_reboot(ctx, pid, &reason, timeout);
}


/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
