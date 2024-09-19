/* Watchdog API for the process supervisor, its clients, and others
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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SYSLOG_NAMES
#include "wdt.h"
#include "private.h"


static int api_init(void)
{
	int sd;
	struct sockaddr_un sun;

	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_PATH);
	if (access(sun.sun_path, F_OK)) {
#ifdef TESTMODE_DISABLED
		return -1;
#else
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_TEST);
		if (access(sun.sun_path, F_OK))
			return -1;
#endif
	}

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (-1 == sd)
		return -1;

	if (connect(sd, (struct sockaddr*)&sun, sizeof(sun)) == -1) {
		if (EINPROGRESS != errno)
			goto error;
	}

	return sd;

error:
	close(sd);
	return -1;
}

static int api_poll(int sd, int ev)
{
	struct pollfd pfd;
	int retries = 3;
	int rc;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd     = sd;
	pfd.events = ev;

	while ((rc = poll(&pfd, 1, 1000)) <= 0) {
		if (-1 == rc && EINTR == errno) {
			if (--retries)
				continue;
		}

		if (rc == 0)
			errno = ETIMEDOUT;

		return 0;
	}

	return 1;
}

/* Used by client to check if server is up */
int wdog_ping(void)
{
	int sd;
	int so_error = ENOTCONN;
	socklen_t len = sizeof(so_error);

	sd = api_init();
	if (-1 == sd)
		return 1;

	if (api_poll(sd, POLLIN | POLLOUT)) {
		if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1)
			goto error;
	} else
		goto error;

	close(sd);
	return so_error != 0;
error:
	close(sd);
	return 1;
}

static int doit(int cmd, int id, char *label, unsigned int timeout, unsigned int *ack)
{
	wdog_t req = {
		.cmd     = cmd,
		.pid     = getpid(),
		.timeout = timeout,
	};
	size_t len;
	int sd;

	sd = api_init();
	if (-1 == sd) {
		if (errno == ENOENT)
			errno = EAGAIN;
		return -errno;
	}

	if (!label || !label[0])
		label = __progname;

	/* Copy label and make sure to terminate it. */
	len = strlen(label);
	if (len >= sizeof(req.label))
		len = sizeof(req.label) - 1;
	strncpy(req.label, label, sizeof(req.label));
	req.label[len] = 0;

	switch (cmd) {
	case WDOG_KICK_CMD:
	case WDOG_UNSUBSCRIBE_CMD:
		req.id  = id;
		req.ack = *ack;
		break;

	default:
		req.id = id;
		break;
	}

	if (api_poll(sd, POLLOUT)) {
		if (write(sd, &req, sizeof(req)) != sizeof(req))
			goto error;
	} else
		goto error;

	if (api_poll(sd, POLLIN)) {
		if (read(sd, &req, sizeof(req)) != sizeof(req))
			goto error;
	} else
		goto error;

	if (req.cmd == WDOG_CMD_ERROR) {
		errno = req.error;
		goto error;
	}
	close(sd);

	if (ack) {
		wdog_reason_t *reason = (wdog_reason_t *)ack;

		switch (cmd) {
		case WDOG_RESET_REASON_CMD:
		case WDOG_RESET_REASON_RAW_CMD:
			memcpy(reason, &req, sizeof(wdog_reason_t));
			reason->label[sizeof(reason->label) - 1] = 0;
			break;
		default:
			*ack = req.next_ack;
			break;
		}
	}

	if (WDOG_SUBSCRIBE_CMD == cmd)
		return req.id;

	return 0;
error:
	close(sd);
	return -errno;
}

int wdog_subscribe(char *label, unsigned int timeout, unsigned int *ack)
{
	return doit(WDOG_SUBSCRIBE_CMD, -1, label, timeout, ack);
}

int wdog_kick(int id, unsigned int timeout, unsigned int ack, unsigned int *next_ack)
{
	int rc;

	rc = doit(WDOG_KICK_CMD, id, NULL, timeout, &ack);
	if (!rc)
		*next_ack = ack;

	return rc;
}

int wdog_extend_kick(int id, unsigned int timeout, unsigned int *ack)
{
	return doit(WDOG_KICK_CMD, id, NULL, timeout, ack);
}

int wdog_kick2(int id, unsigned int *ack)
{
	return doit(WDOG_KICK_CMD, id, NULL, 0, ack);
}

int wdog_list_clients(void)
{
	return doit(WDOG_LIST_SUPV_CLIENTS_CMD, 0, NULL, 0, NULL);
}

int wdog_unsubscribe(int id, unsigned int ack)
{
	return doit(WDOG_UNSUBSCRIBE_CMD, id, NULL, 0, &ack);
}

int wdog_set_debug(int enable)
{
	return doit(WDOG_SET_DEBUG_CMD, !!enable, NULL, 0, NULL);
}

int wdog_get_debug(int *status)
{
	return doit(WDOG_GET_DEBUG_CMD, 0, NULL, 0, (unsigned int *)status);
}

int __wdog_loglevel(char *level)
{
	for (int i = 0; prioritynames[i].c_name; i++) {
		if (string_match(prioritynames[i].c_name, level))
			return prioritynames[i].c_val;
	}

	return atoi(level);
}

const char *__wdog_levellog(int log)
{
	for (int i = 0; prioritynames[i].c_name; i++) {
		if (prioritynames[i].c_val == log)
			return prioritynames[i].c_name;
	}

	return NULL;
}

int wdog_set_loglevel(char *level)
{
	int val;

	val = __wdog_loglevel(level);
	if (val < LOG_EMERG || val > LOG_DEBUG)
		return -1;

	return doit(WDOG_SET_LOGLEVEL_CMD, val, NULL, 0, NULL);
}

char *wdog_get_loglevel(void)
{
	int val;

	if (doit(WDOG_GET_LOGLEVEL_CMD, 0, NULL, 0, (unsigned int *)&val))
		return NULL;

	return (char *)__wdog_levellog(val);
}

int wdog_enable(int enable)
{
	return doit(WDOG_ENABLE_CMD, !!enable, NULL, 0, NULL);
}

int wdog_status(int *status)
{
	return doit(WDOG_STATUS_CMD, 0, NULL, 0, (unsigned int *)status);
}

int wdog_failed(wdog_code_t code, int pid, char *label, unsigned int timeout)
{
	return doit(code + WDOG_FAILED_BASE_CMD, pid, label, timeout, NULL);
}

int wdog_reset(pid_t pid, char *label)
{
	return doit(WDOG_RESET_CMD, pid, label, 0, NULL);
}

/*
 * Called by init to signal pending reboot
 *
 * When a user requests a reboot, init (PID 1) is usually the process
 * responsible for performing an orderly shutdown, taking the system
 * down safely: sending SIGTERM to all services, syncing and unmounting
 * file systems etc.
 *
 * This function is what init can use to order watchdogd to save the
 * reset reason before initiating the shutdown.  When this function has
 * been called, with a reasonable timeout, watchdogd will go into a
 * special mode waiting only for SIGTERM.
 *
 * The timeout is the amount of time, in milliseconds, that watchdogd
 * will wait for SIGTERM before exiting and handing over the reset to
 * the WDT.  If SIGTERM is received within timeout no warning is sent to
 * the log and watchdogd simply exits, pending for WDT reset.
 */
int wdog_reset_timeout(pid_t pid, char *label, unsigned int timeout)
{
	return doit(WDOG_RESET_CMD, pid, label, timeout, NULL);
}

int wdog_reset_counter(unsigned int *counter)
{
	if (!counter) {
		errno = EINVAL;
		return -1;
	}

	return doit(WDOG_RESET_COUNTER_CMD, 0, NULL, 0, counter);
}

int wdog_reset_reason(wdog_reason_t *reason)
{
	if (!reason) {
		errno = EINVAL;
		return -1;
	}

	return doit(WDOG_RESET_REASON_CMD, 0, NULL, 0, (unsigned int *)reason);
}

int wdog_reset_reason_raw(wdog_reason_t *reason)
{
	if (!reason) {
		errno = EINVAL;
		return -1;
	}

	return doit(WDOG_RESET_REASON_RAW_CMD, 0, NULL, 0, (unsigned int *)reason);
}

char *wdog_reset_reason_str(wdog_reason_t *reason)
{
	if (!reason) {
		errno = EINVAL;
		return "Unknown";
	}

	switch (reason->code) {
	case WDOG_SYSTEM_NONE:
		return "None";

	case WDOG_SYSTEM_OK:
		return "System OK";

	case WDOG_FAILED_SUBSCRIPTION:
		return "Failed subscription";

	case WDOG_FAILED_KICK:
		return "Failed kick";

	case WDOG_FAILED_UNSUBSCRIPTION:
		return "Failed unsubscription";

	case WDOG_FAILED_TO_MEET_DEADLINE:
		return "Failed to meet deadline";

	case WDOG_FORCED_RESET:
		return "Forced reset";

	case WDOG_DESCRIPTOR_LEAK:
		return "Descriptor leak";

	case WDOG_MEMORY_LEAK:
		return "Memory leak";

	case WDOG_CPU_OVERLOAD:
		return "CPU overload";

	case WDOG_FAILED_UNKNOWN:
	default:
		break;
	}

	return "Unknown failure";
}

int wdog_reset_reason_clr(void)
{
	return doit(WDOG_CLEAR_REASON_CMD, -1, NULL, 0, NULL);
}

int wdog_reload(void)
{
	return doit(WDOG_RELOAD_CMD, -1, NULL, 0, NULL);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
