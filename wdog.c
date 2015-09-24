/* Watchdog API for pmon and its clients
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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "wdog.h"
#include "wdt.h"

/*
 * Hidden test mode.  Set by daemon when in --test-mode,
 * or by examples when testing the API.
 */
int __wdog_testmode = 0;


/* Hidden API */
int wdog_pmon_api_init(int server)
{
	int sd;
	struct sockaddr_un sun;

	sun.sun_family = AF_UNIX;
	if (__wdog_testmode)
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_PMON_BASENAME);
	else
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_PMON_PATH);

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (-1 == sd)
		return -1;

	if (server) {
		remove(sun.sun_path);

		if (-1 == bind(sd, (struct sockaddr*)&sun, sizeof(sun)))
			goto error;

		if (-1 == listen(sd, 10))
			goto error;
	} else {
		if (connect(sd, (struct sockaddr*)&sun, sizeof(sun)) == -1)
			if (EINPROGRESS != errno)
				goto error;
	}

	return sd;

error:
	close(sd);
	return -1;
}

static int sock_poll(int sd, int ev)
{
	struct pollfd pfd;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd     = sd;
	pfd.events = ev;
	if (poll(&pfd, 1, 1000) == 1)
		return 1;

	return 0;
}

/* Used by client to check if server is up */
int wdog_pmon_ping(void)
{
	int sd;
	int so_error = ENOTCONN;
	socklen_t len = sizeof(so_error);

	sd = wdog_pmon_api_init(0);
	if (-1 == sd)
		return 1;

	if (sock_poll(sd, POLLIN | POLLOUT))
		getsockopt(sd, SOL_SOCKET, SO_ERROR, &so_error, &len);

	close(sd);

	return errno != 0;
}

static int doit(int cmd, int id, char *label, int timeout, int *ack)
{
	int sd;
	wdog_pmon_t req = {
		.cmd     = cmd,
		.pid     = getpid(),
		.timeout = timeout,
	};

	sd = wdog_pmon_api_init(0);
	if (-1 == sd) {
		if (errno == ENOENT)
			errno = EAGAIN;
		return -errno;
	}

	if (!label || !label[0])
		label = __progname;

	strncpy(req.label, label, sizeof(req.label));
	switch (cmd) {
	case WDOG_PMON_KICK_CMD:
	case WDOG_PMON_UNSUBSCRIBE_CMD:
		req.id  = id;
		req.ack = *ack;
		break;

	default:
		req.id = id;
		break;
	}

	if (sock_poll(sd, POLLOUT)) {
		if (write(sd, &req, sizeof(req)) != sizeof(req))
			goto error;
	}

	if (sock_poll(sd, POLLIN)) {
		if (read(sd, &req, sizeof(req)) != sizeof(req))
			goto error;
	}

	if (req.cmd == WDOG_PMON_CMD_ERROR) {
		errno = req.error;
		goto error;
	}

	*ack = req.next_ack;
	close(sd);

	if (WDOG_PMON_SUBSCRIBE_CMD == cmd)
		return req.id;

	return 0;
error:
	close(sd);
	return -errno;
}

int wdog_pmon_subscribe(char *label, int timeout, int *ack)
{
	return doit(WDOG_PMON_SUBSCRIBE_CMD, -1, label, timeout, ack);
}

int wdog_pmon_kick(int id, int *ack)
{
	return doit(WDOG_PMON_KICK_CMD, id, NULL, -1, ack);
}

int wdog_pmon_unsubscribe(int id, int ack)
{
	return doit(WDOG_PMON_UNSUBSCRIBE_CMD, id, NULL, -1, &ack);
}

int wdog_enable(int enable)
{
	return doit(WDOG_ENABLE_CMD, !!enable, NULL, -1, NULL);
}

int wdog_status(int *enabled)
{
	return doit(WDOG_STATUS_CMD, 0, NULL, -1, enabled);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
