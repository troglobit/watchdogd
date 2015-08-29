/* Process monitor
 *
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

#include <sys/socket.h>

#include "wdt.h"
#include "api.h"
#include "pmon.h"

typedef struct {
	int   id;		/* 0-255, -1: Free */
	pid_t pid;
	char  label[16];	/* Process name, or label. */
	int   timeout;		/* Period time, in msec. */
	uev_t watcher;		/* Process timer */
	int   ack;		/* Next expected ACK from process */
} pmon_t;

static uev_t   watcher;
static pmon_t  process[256];	/* Max ID 0-255 */


static pmon_t *allocate(pid_t pid, char *label, int timeout)
{
	size_t i;
	pmon_t *p = NULL;

	if (!label || timeout < WDOG_PMON_MIN_TIMEOUT) {
		errno = EINVAL;
		return NULL;
	}

	for (i = 0; i < NELEMS(process); i++) {
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

static void release(pmon_t *p)
{
	memset(p, 0, sizeof(*p));
	p->id = -1;
}

/* Validate user's kick/unsubscribe against our records */
static pmon_t *get(int id, pid_t pid, int ack)
{
	pmon_t *p;

	if (id < 0 || id >= (int)NELEMS(process)) {
		errno = EINVAL;
		return NULL;
	}

	p = &process[id];
	if (p->pid != pid) {
		INFO("Invalid pid %d for registered pid %d", pid, p->pid);
		errno = EBADE;
		return NULL;
	}

	if (p->ack != ack) {
		INFO("BAD next ack for pid %d, was %d, expected ack %d", pid, ack, p->ack);
		errno = EBADRQC;
		return NULL;
	}

	return p;
}

/* XXX: Use a random next-ack != req.ack */
static void next_ack(pmon_t *p, wdog_pmon_t *req)
{
	p->ack        += 2;	/* FIXME */

	req->id        = p->id;
	req->next_ack  = p->ack;
}

/* Client timed out.  Store its label in reset-cause, sync and reboot */
static void timeout(uev_t *w, void *arg, int UNUSED(events))
{
	pmon_t *p = (pmon_t *)arg;

	if (system("date"))
		perror("system");
		
	ERROR("Process %d, label '%s' failed to meet its deadline, rebooting ...",
	      p->pid, p->label);

	/* XXX */
	WARN("Process %d had timeout %d msec ...", p->pid, w->period);
}

/* Client connected to domain socket sent a request */
static void cb(uev_t *w, void *UNUSED(arg), int UNUSED(events))
{
	int sd = accept(w->fd, NULL, NULL);
	wdog_pmon_t req;
	pmon_t *p;
	ssize_t num;

	if (-1 == sd) {
		WARN("pmon failed accepting incoming client connection");
		return;
	}

	num = read(sd, &req, sizeof(req));
	if (num <= 0) {
		close(sd);
		if (num < 0)
			WARN("Failed reading client request");
		
		return;
	}

	switch (req.cmd) {
	case WDOG_PMON_SUBSCRIBE_CMD:
		/* Start timer, return ID from allocated timer. */
		INFO("Hello %s, registering pid %d ...", req.label, req.pid);
		p = allocate(req.pid, req.label, req.timeout);
		if (p) {
			next_ack(p, &req);
			INFO("pid %d next ack => %d", req.pid, req.next_ack);
			uev_timer_init(w->ctx, &p->watcher, timeout, p, p->timeout, p->timeout);
		}
		break;

	case WDOG_PMON_UNSUBSCRIBE_CMD:
		/* Unregister timer and free it. */
		p = get(req.id, req.pid, req.ack);
		if (!p) {
			PERROR("Process %d tried to unsubscribe using invalid credentials", req.pid);
			req.cmd = WDOG_PMON_CMD_ERROR;
		} else {
			uev_timer_stop(&p->watcher);
			release(p);
			INFO("Goodbye %s (pid:%d), id:%d ...", req.label, req.pid, req.id);
		}
		break;

	case WDOG_PMON_KICK_CMD:
		/* Check next_ack from client, restart timer if OK, otherwise force reboot */
		p = get(req.id, req.pid, req.ack);
		if (!p) {
			PERROR("Process %d tried to kick using invalid credentials", req.pid);
			req.cmd = WDOG_PMON_CMD_ERROR;
		} else {
			INFO("How do you do %s (pid %d), id:%d -- ACK should be %d, is %d",
			      req.label, req.pid, req.id, p->ack, req.ack);
			next_ack(p, &req);
			uev_timer_set(&p->watcher, p->timeout, p->timeout);
		}
		break;

	default:
		ERROR("pmon: Invalid command %d", req.cmd);
		break;
	}

	if (write(sd, &req, sizeof(req)) != sizeof(req))
		WARN("Failed sending reply to client %s, id:%d", req.label, req.id);

	close(sd);
}

/* In libwdog.a */
extern int wdog_pmon_api_init(int server);

int pmon_init(uev_ctx_t *ctx, int UNUSED(T))
{
	int sd;
	size_t i;

	INFO("Starting process heartbeat monitor, waiting for process subscribe ...");

	/* XXX: Maybe store these in shm instead, in case we are restarted? */
	for (i = 0; i < NELEMS(process); i++) {
		memset(&process[i], 0, sizeof(pmon_t));
		process[i].id = -1;
	}

	sd = wdog_pmon_api_init(1);
	if (sd < 0) {
		PERROR("Failed starting pmon");
		return 1;
	}
		
	return uev_io_init(ctx, &watcher, cb, NULL, sd, UEV_READ);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
