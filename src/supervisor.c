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
#include "wdt.h"
#include "private.h"
#include "rr.h"
#include "wdog.h"
#include "script.h"
#include "supervisor.h"

static struct supervisor {
	int   id;		/* 0-255, -1: Free */
	pid_t pid;
	char  label[48];	/* Process name, or label. */
	int   timeout;		/* Period time, in msec. */
	uev_t watcher;		/* Process timer */
	int   ack;		/* Next expected ACK from process */
} process[256];                 /* Max ID 0-255 */

static int supervisor_enabled = 0;
static int supervisor_realtime = 0;
static char *exec = NULL;


static struct supervisor *find_supervised(pid_t pid)
{
	size_t i;

	for (i = 0; i < NELEMS(process); i++) {
		if (process[i].id == -1)
			continue;

		if (process[i].pid == pid)
			return &process[i];
	}

	return NULL;
}

static void release(struct supervisor *p)
{
	uev_timer_stop(&p->watcher);
	memset(p, 0, sizeof(*p));
	p->id = -1;
}

static int action(uev_ctx_t *ctx, struct supervisor *p, wdog_code_t c, int timeout)
{
	wdog_reason_t reason;

	if (exec && !access(exec, X_OK)) {
		int rc;

		/*
		 * If script returns OK we expect it to have dealt with
		 * the situation properly.  So we remove this process
		 * from our supervision and go about our business.
		 */
		rc = supervisor_exec(exec, c, p->pid, p->label);
		if (!rc) {
			release(p);
			return 0;
		}
	}

	memset(&reason, 0, sizeof(reason));
	reason.wid = p->id;
	reason.code = c;
	strlcpy(reason.label, p->label, sizeof(reason.label));

	return wdt_reset(ctx, p->pid, &reason, timeout);
}

static void fail(uev_ctx_t *ctx, wdog_t *req, wdog_code_t c, const char *msg)
{
	struct supervisor *p;

	PERROR("%s[%d] %s", req->label, req->pid, msg);
	p = find_supervised(req->pid);
	if (p)
		action(ctx, p, c, 0);
}

static int supervisor_action(uev_ctx_t *ctx, wdog_t *req, int is_reset)
{
	struct supervisor *p;
	wdog_code_t cause;
	char *label = req->label;
	int timeout;

	if (is_reset) {
		cause = WDOG_FAILED_TO_MEET_DEADLINE;
		timeout = (int)req->timeout;
	} else {
		cause = req->cmd - WDOG_FAILED_BASE_CMD;
		timeout = req->timeout > 0 ? (int)req->timeout : -1;
	}

	p = find_supervised(req->id);
	if (p) {
		if (!string_compare(req->label, WDOG_RESET_STR_DEFAULT))
			strlcpy(p->label, req->label, sizeof(p->label));

		return action(ctx, p, cause, timeout);
	}

	if (timeout < 0)
		return 1;	/* Cannot find failed PID */

	return wdt_forced_reset(ctx, req->id, label, timeout);
}

static int supervisor_failed(uev_ctx_t *ctx, wdog_t *req)
{
	return supervisor_action(ctx, req, 0);
}

static int supervisor_reset(uev_ctx_t *ctx, wdog_t *req)
{
	return supervisor_action(ctx, req, 1);
}

/*
 * If any process is being supervised/subscribed, and watchdogd is
 * enabled, we raise the RT priority to 98 (just below the kernel WDT in
 * prio).  This to ensure that system monitoring goes before anything
 * else in the system.
 */
static void set_priority(int enabled, int rtprio)
{
	int result = 0;
	struct sched_param prio;

	if (enabled) {
		DEBUG("Setting SCHED_RR rtprio %d", rtprio);
		prio.sched_priority = rtprio;
		result = sched_setscheduler(getpid(), SCHED_RR, &prio);
	} else {
		DEBUG("Setting SCHED_OTHER prio %d", 0);
		prio.sched_priority = 0;
		result = sched_setscheduler(getpid(), SCHED_OTHER, &prio);
	}

	if (result && !wdt_testmode())
		PERROR("Failed setting process %spriority",
		       enabled ? "realtime " : "");
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
		DEBUG("BAD next ack for %s[%d] was %d, expected %d",
		      p->label, pid, ack, p->ack);
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

/* Client timed out.  Save pid & label in reset reason, sync and reboot */
static void timeout_cb(uev_t *w, void *arg, int events)
{
	struct supervisor *p = (struct supervisor *)arg;

	EMERG("Process %s[%d] failed to meet its deadline, rebooting ...",
	      p->label, p->pid);
	action(w->ctx, p, WDOG_FAILED_TO_MEET_DEADLINE, 0);
}

int supervisor_cmd(uev_ctx_t *ctx, wdog_t *req)
{
	struct supervisor *p;
	wdog_reason_t *reason;

	if (!supervisor_enabled)
		return 1;

	switch (req->cmd) {
	case WDOG_SUBSCRIBE_CMD:
		/* Start timer, return ID from allocated timer. */
		DEBUG("Hello %s[%d].", req->label, req->pid);
		p = allocate(req->pid, req->label, req->timeout);
		if (!p) {
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		} else {
			next_ack(p, req);
			DEBUG("%s[%d] next ack: %d", req->label, req->pid,
			      req->next_ack);

			/* Allow for some scheduling slack */
			uev_timer_init(ctx, &p->watcher, timeout_cb, p,
				       p->timeout + 500, p->timeout + 500);
		}
		break;

	case WDOG_UNSUBSCRIBE_CMD:
		/* Unregister timer and free it. */
		p = get(req->id, req->pid, req->ack);
		if (!p) {
			fail(ctx, req, WDOG_FAILED_UNSUBSCRIPTION,
			     "tried to unsubscribe with invalid credentials");
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		} else {
			release(p);
			DEBUG("Goodbye %s[%d] id:%d.", req->label, req->pid, req->id);
		}
		break;

	case WDOG_KICK_CMD:
		/*
		 * Check next_ack from client, restart timer if OK,
		 * otherwise force reboot
		 */
		p = get(req->id, req->pid, req->ack);
		if (!p) {
			fail(ctx, req, WDOG_FAILED_KICK, "tried to kick with invalid credentials");
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		} else {
			int timeout = p->timeout;

			/*
			 * If process needs to request an extended timemout
			 * Like in subscribe we allow for some scheduling slack
			 */
			if (req->timeout > 0)
				timeout = req->timeout + 500;

			DEBUG("How do you do %s[%d], id:%d?  ACK should be %d, is %d",
			      req->label, req->pid, req->id, p->ack, req->ack);
			next_ack(p, req);
			if (enabled)
				uev_timer_set(&p->watcher, timeout, timeout);
		}
		break;

	case WDOG_RESET_COUNTER_CMD:
		req->next_ack = wdt_reset_counter();
		break;

	case WDOG_RESET_CMD:
		if (supervisor_reset(ctx, req)) {
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		}
		break;

	case WDOG_FAILED_SYSTEMOK_CMD...WDOG_FAILED_OVERLOAD_CMD:
		if (supervisor_failed(ctx, req)) {
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		}
		break;

	case WDOG_RESET_REASON_CMD:
		reason = (wdog_reason_t *)req;
		*reason = reset_reason;
		break;

	case WDOG_RESET_REASON_RAW_CMD:
		reason = (wdog_reason_t *)req;
		if (reset_reason_get(reason, NULL)) {
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		}
		break;

	case WDOG_CLEAR_REASON_CMD:
		if (reset_reason_clear(NULL)) {
			req->cmd   = WDOG_CMD_ERROR;
			req->error = errno;
		}
		break;

	default:
		return 1;
	}

	return 0;
}

int supervisor_init(uev_ctx_t *ctx, int enabled, int realtime, char *script)
{
	static int already = 0;
	size_t i;

	/* XXX: Maybe store these in shm instead, in case we are restarted? */
	for (i = 0; !already && i < NELEMS(process); i++) {
		memset(&process[i], 0, sizeof(struct supervisor));
		process[i].id = -1;
	}
	already = 1;

	supervisor_enabled = enabled;
	if (!enabled) {
		INFO("Process supervisor disabled.");
		return supervisor_enable(0);
	}
	if (script) {
		if (exec)
			free(exec);
		exec = strdup(script);
	}

	INFO("Starting process supervisor, waiting for client subscribe ...");
	supervisor_realtime = realtime;
	set_priority(1, realtime);

	return 0;
}

int supervisor_exit(uev_ctx_t *ctx)
{
	size_t i;

	if (!supervisor_enabled)
		return 0;

	for (i = 0; i < NELEMS(process); i++) {
		if (process[i].id != -1) {
			uev_timer_stop(&process[i].watcher);

			memset(&process[i], 0, sizeof(struct supervisor));
			process[i].id = -1;
		}
	}

	set_priority(0, 0);

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
			DEBUG("%sabling supervisor for %s, id:%d ...",
			      enable ? "En" : "Dis", p->label, p->id);
			if (!enable)
				result += uev_timer_stop(&p->watcher);
			else
				result += uev_timer_set(&p->watcher,
							p->timeout, p->timeout);
		}
	}

	set_priority(enable, supervisor_realtime);

	return result;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
