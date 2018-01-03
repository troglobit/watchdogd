/* Process supervisor plugin
 *
 * Copyright (C) 2015-2018  Joachim Nilsson <troglobit@gmail.com>
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

#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "plugin.h"
#include "private.h"
#include "rc.h"
#include "wdog.h"
#include "pmon.h"

static struct supervisor {
	int   id;		/* 0-255, -1: Free */
	pid_t pid;
	char  label[16];	/* Process name, or label. */
	int   timeout;		/* Period time, in msec. */
	uev_t watcher;		/* Process timer */
	int   ack;		/* Next expected ACK from process */
} process[256];                 /* Max ID 0-255 */

static int     sd = -1;
static int     active = 0;
static int     rtprio = 98;
static int     supervisor_enabled = 0;
static uev_t   watcher;


static size_t num_supervised(void)
{
	size_t i, num = 0;

	for (i = 0; i < NELEMS(process); i++) {
		if (process[i].id != -1)
			num++;
	}

	return num;
}

/*
 * If any process is being supervised/subscribed, and watchdogd is
 * enabled, we raise the RT priority to 98 (just below the kernel WDT in
 * prio).  This to ensure that system monitoring goes before anything
 * else in the system.
 */
static void set_priority(void)
{
	int result = 0;
	struct sched_param prio;

	if (num_supervised() && enabled) {
		if (!active) {
			prio.sched_priority = 98;
			DEBUG("Setting SCHED_RR rtprio %d", prio.sched_priority);
			result = sched_setscheduler(getpid(), SCHED_RR, &prio);
			active = 1;
		}
	} else {
		if (active) {
			prio.sched_priority = 0;
			DEBUG("Setting SCHED_OTHER prio %d", prio.sched_priority);
			result = sched_setscheduler(getpid(), SCHED_OTHER, &prio);
			active = 0;
		}
	}

	if (result && !wdt_testmode())
		PERROR("Failed setting process %spriority", active ? "realtime " : "");
}

/*
 * Create supervisor for client process
 *
 * Returns:
 * A pointer to a enw supervisor object, with @pid, @label and @timeout
 * filled in, or %NULL on error, with @errno set to one of:
 * - %EINVAL when no label was given, or @timeout < %WDOG_SUPERVISOR_MIN_TIMEOUT
 * - %ENOMEM when MAX number of monitored processes has been reached
 */
static struct supervisor *allocate(pid_t pid, char *label, unsigned int timeout)
{
	size_t i;
	struct supervisor *p = NULL;

	if (!label || timeout < WDOG_SUPERVISOR_MIN_TIMEOUT) {
		errno = EINVAL;
		return NULL;
	}

	/* Reserve id:0 for watchdogd itself */
	for (i = 1; i < NELEMS(process); i++) {
		if (process[i].id == -1) {
			p = &process[i];
			p->id = i;
			p->pid = pid;
			p->timeout = timeout;
			p->ack = 40;
			strlcpy(p->label, label, sizeof(p->label));
			break;
		}
	}

	if (!p)
		errno = ENOMEM;

	return p;
}

static void release(struct supervisor *p)
{
	memset(p, 0, sizeof(*p));
	p->id = -1;
}

/*
 * Validate user's kick/unsubscribe against our records
 *
 * Returns:
 * Pointer supervisor object, or %NULL on error, with errno set to one of:
 * - %EINVAL when the given ID is out of bounds
 * - %EIDRM when daemon was restarted or client disconnected but keeps kicking
 * - %EBADE when someone else tries to kick on behalf of client
 * - %EBADRQC when client uses the wrong ack code
 */
static struct supervisor *get(int id, pid_t pid, int ack)
{
	struct supervisor *p;

	if (id < 0 || id >= (int)NELEMS(process)) {
		errno = EINVAL;
		return NULL;
	}

	p = &process[id];
	if (p->pid != pid) {
		if (!p->pid)
			errno = EIDRM;
		else
			errno = EBADE;

		return NULL;
	}

	if (p->ack != ack) {
		DEBUG("BAD next ack for %s[%d] was %d, expected %d", p->label, pid, ack, p->ack);
		errno = EBADRQC;
		return NULL;
	}

	return p;
}

/* XXX: Use a random next-ack != req.ack */
static void next_ack(struct supervisor *p, wdog_t *req)
{
	p->ack        += 2;	/* FIXME */

	req->id        = p->id;
	req->next_ack  = p->ack;
}

/* Client timed out.  Store its label in reset-cause, sync and reboot */
static void timeout(uev_t *w, void *arg, int events)
{
	struct supervisor *p = (struct supervisor *)arg;
	wdog_reason_t reason;

	ERROR("Process %s[%d] failed to meet its deadline, rebooting ...", p->label, p->pid);

	memset(&reason, 0, sizeof(reason));
	reason.wid = p->id;
	reason.cause = WDOG_FAILED_TO_MEET_DEADLINE;
	strlcpy(reason.label, p->label, sizeof(reason.label));
	wdt_reboot(w->ctx, p->pid, &reason, 0);
}

/* Client connected to domain socket sent a request */
static void cb(uev_t *w, void *arg, int events)
{
	int sd;
	struct supervisor *p;
	ssize_t num;
	wdog_t req;
	wdog_reason_t *reason;

	sd = accept(w->fd, NULL, NULL);
	if (-1 == sd) {
		WARN("Failed accepting incoming client connection");
		return;
	}

	num = read(sd, &req, sizeof(req));
	if (num <= 0) {
		close(sd);
		if (num < 0)
			WARN("Failed reading client request");

		return;
	}

	/* Make sure to terminate string, needed below. */
	req.label[sizeof(req.label) - 1] = 0;

	switch (req.cmd) {
	case WDOG_SUBSCRIBE_CMD:
		/* Start timer, return ID from allocated timer. */
		DEBUG("Hello %s[%d].", req.label, req.pid);
		p = allocate(req.pid, req.label, req.timeout);
		if (!p) {
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		} else {
			next_ack(p, &req);
			DEBUG("%s[%d] next ack: %d", req.label, req.pid, req.next_ack);

			/* Allow for some scheduling slack */
			uev_timer_init(w->ctx, &p->watcher, timeout, p, p->timeout + 500, p->timeout + 500);
		}
		break;

	case WDOG_UNSUBSCRIBE_CMD:
		/* Unregister timer and free it. */
		p = get(req.id, req.pid, req.ack);
		if (!p) {
			PERROR("%s[%d] tried to unsubscribe using invalid credentials", req.label, req.pid);
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		} else {
			uev_timer_stop(&p->watcher);
			release(p);
			DEBUG("Goodbye %s[%d] id:%d.", req.label, req.pid, req.id);
		}
		break;

	case WDOG_KICK_CMD:
		/* Check next_ack from client, restart timer if OK, otherwise force reboot */
		p = get(req.id, req.pid, req.ack);
		if (!p) {
			PERROR("%s[%d] tried to kick using invalid credentials", req.label, req.pid);
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		} else {
			int timeout = p->timeout;

			/*
			 * If process needs to request an extended timemout
			 * Like in subscribe we allow for some scheduling slack
			 */
			if (req.timeout > 0)
				timeout = req.timeout + 500;

			DEBUG("How do you do %s[%d], id:%d -- ACK should be %d, is %d",
			      req.label, req.pid, req.id, p->ack, req.ack);
			next_ack(p, &req);
			if (enabled)
				uev_timer_set(&p->watcher, timeout, timeout);
		}
		break;

	case WDOG_ENABLE_CMD:
		req.next_ack = wdt_enable(req.id);
		break;

	case WDOG_STATUS_CMD:
		req.next_ack = enabled;
		break;

	case WDOG_SET_DEBUG_CMD:
		req.next_ack = wdt_debug(req.id);
		break;

	case WDOG_GET_DEBUG_CMD:
		req.next_ack = loglevel == LOG_DEBUG;
		break;

	case WDOG_SET_LOGLEVEL_CMD:
		loglevel = req.id;
		setlogmask(LOG_UPTO(loglevel));
		break;

	case WDOG_GET_LOGLEVEL_CMD:
		req.next_ack = loglevel;
		break;

	case WDOG_REBOOT_CMD:
		if (wdt_forced_reboot(w->ctx, req.id, req.label, req.timeout)) {
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		}
		break;

	case WDOG_RESET_COUNTER_CMD:
		req.next_ack = wdt_reset_counter();
		break;

	case WDOG_RESET_CAUSE_CMD:
		reason = (wdog_reason_t *)&req;
		*reason = reboot_reason;
		break;

	case WDOG_RESET_CAUSE_RAW_CMD:
		reason = (wdog_reason_t *)&req;
		if (reset_cause_get(reason, NULL)) {
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		}
		break;

	case WDOG_CLEAR_CAUSE_CMD:
		if (reset_cause_clear(NULL)) {
			req.cmd   = WDOG_CMD_ERROR;
			req.error = errno;
		}
		break;

	default:
		ERROR("Invalid command %d", req.cmd);
		req.cmd   = WDOG_CMD_ERROR;
		req.error = EBADMSG;
		break;
	}

	if (write(sd, &req, sizeof(req)) != sizeof(req))
		WARN("Failed sending reply to %s[%d]", req.label, req.pid);

	shutdown(sd, SHUT_RDWR);
	close(sd);

	set_priority();
}

static int api_init(void)
{
	int sd;
	struct sockaddr_un sun;

	sun.sun_family = AF_UNIX;
	if (wdt_testmode())
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_TEST);
	else
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_PATH);

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (-1 == sd)
		return -1;

	if (remove(sun.sun_path) && errno != ENOENT)
		PERROR("Failed removing %s", sun.sun_path);

	if (-1 == bind(sd, (struct sockaddr*)&sun, sizeof(sun)))
		goto error;

	if (-1 == listen(sd, 10))
		goto error;

	return sd;

error:
	close(sd);
	return -1;
}

int supervisor_init(uev_ctx_t *ctx, int T)
{
	size_t i;

	if (!supervisor_enabled) {
		INFO("Process supervisor disabled.");
		return 1;
	}

	if (sd != -1) {
		ERROR("Process supervisor already started.");
		return 1;
	}

	INFO("Starting process supervisor, waiting for client subscribe ...");

	/* XXX: Maybe store these in shm instead, in case we are restarted? */
	for (i = 0; i < NELEMS(process); i++) {
		memset(&process[i], 0, sizeof(struct supervisor));
		process[i].id = -1;
	}

	sd = api_init();
	if (sd < 0) {
		PERROR("Failed starting process supervisor");
		return 1;
	}

	return uev_io_init(ctx, &watcher, cb, NULL, sd, UEV_READ);
}

int supervisor_exit(uev_ctx_t *ctx)
{
	size_t i;

	if (!supervisor_enabled)
		return 0;

	uev_io_stop(&watcher);
	shutdown(sd, SHUT_RDWR);
	(void)remove(WDOG_SUPERVISOR_PATH);
	(void)remove(WDOG_SUPERVISOR_TEST);
	close(sd);
	sd = -1;

	for (i = 0; i < NELEMS(process); i++) {
		if (process[i].id != -1) {
			uev_timer_stop(&process[i].watcher);

			memset(&process[i], 0, sizeof(struct supervisor));
			process[i].id = -1;
		}
	}

	set_priority();

	return 0;
}

/*
 * Disable the supervisor plugin when watchdogd is disabled
 */
int supervisor_enable(int enable)
{
	int    result = 0;
	size_t i;

	for (i = 0; i < NELEMS(process); i++) {
		struct supervisor *p = &process[i];

		if (p->id != -1) {
			if (!enable)
				result += uev_timer_stop(&p->watcher);
			else
				result += uev_timer_set(&p->watcher, p->timeout, p->timeout);
		}
	}

	set_priority();

	return result;
}

int supervisor_set(char *optarg)
{
	long long min = sched_get_priority_min(SCHED_RR);
	long long max = sched_get_priority_max(SCHED_RR);

	supervisor_enabled = 1;

	if (optarg) {
		const char *errstr = NULL;

		rtprio = strtonum(optarg, min, max, &errstr);
		if (errstr) {
			ERROR("Watchdog RT priority '%s' is %s!", optarg, errstr);
			return 1;
		}
	} else {
		rtprio = max - 1;
	}

	return 0;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
