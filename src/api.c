/* Server side of libwdog API
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

#include <uev/uev.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "wdt.h"
#include "conf.h"

static int     sd = -1;
static uev_t   watcher;

extern int supervisor_cmd(uev_ctx_t *ctx, wdog_t *req);
extern const char *__wdog_levellog(int log);


/* Client connected to domain socket sent a request */
static void cmd(uev_t *w, void *arg, int events)
{
	const char *tmp;
	ssize_t num;
	wdog_t req;
	int sd;

	DEBUG("Waking up");
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
	DEBUG("cmd %d", req.cmd);

	switch (req.cmd) {
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
		tmp = __wdog_levellog(req.id);
		if (!tmp) {
			req.cmd = WDOG_CMD_ERROR;
			req.error = EINVAL;
		} else {
			LOG("Changing log level %s --> %s", __wdog_levellog(loglevel) , tmp);
			loglevel = req.id;
			setlogmask(LOG_UPTO(loglevel));
		}
		break;

	case WDOG_GET_LOGLEVEL_CMD:
		req.next_ack = loglevel;
		break;

	case WDOG_RELOAD_CMD:
		INFO("Reloading %s", opt_config ?: "nothing");
		if (!conf_parse_file(w->ctx, opt_config))
			wdt_init(w->ctx, NULL);
		break;

	case WDOG_SUBSCRIBE_CMD:
	case WDOG_UNSUBSCRIBE_CMD:
	case WDOG_KICK_CMD:
	case WDOG_RESET_CMD:
	case WDOG_RESET_COUNTER_CMD:
	case WDOG_RESET_REASON_CMD:
	case WDOG_RESET_REASON_RAW_CMD:
	case WDOG_CLEAR_REASON_CMD:
	case WDOG_FAILED_SYSTEMOK_CMD...WDOG_FAILED_OVERLOAD_CMD:
	case WDOG_LIST_SUPV_CLIENTS_CMD:
		DEBUG("Delegating %d to supervisor", req.cmd);
		if (supervisor_cmd(w->ctx, &req)) {
			req.cmd = WDOG_CMD_ERROR;
			req.error = EOPNOTSUPP;
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
}

int api_init(uev_ctx_t *ctx)
{
	struct sockaddr_un sun;

	if (sd != -1) {
		ERROR("Client API socket already started.");
		return 1;
	}

	sun.sun_family = AF_UNIX;
	if (wdt_testmode())
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_TEST);
	else
		snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", WDOG_SUPERVISOR_PATH);

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (-1 == sd)
		goto error;

	if (remove(sun.sun_path) && errno != ENOENT)
		PERROR("Failed removing %s", sun.sun_path);

	if (-1 == bind(sd, (struct sockaddr*)&sun, sizeof(sun)))
		goto error;

	if (-1 == listen(sd, 10))
		goto error;

	return uev_io_init(ctx, &watcher, cmd, NULL, sd, UEV_READ);

error:
	PERROR("Failed starting process supervisor");
	if (sd >= 0)
		close(sd);

	return -1;
}

int api_exit(void)
{
	uev_io_stop(&watcher);
	shutdown(sd, SHUT_RDWR);
	(void)remove(WDOG_SUPERVISOR_PATH);
	(void)remove(WDOG_SUPERVISOR_TEST);
	close(sd);
	sd = -1;

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
