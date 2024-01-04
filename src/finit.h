/* Finit (PID 1) API
 *
 * Copyright (C) 2017-2024  Joachim Wiberg <troglobit@gmail.com>
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

#ifndef WDOG_FINIT_H_
#define WDOG_FINIT_H_

#define INIT_SOCKET             _PATH_VARRUN "finit/socket"
#define INIT_MAGIC              0x03091969
#define INIT_CMD_WDOG_HELLO     128  /* Watchdog register and hello */
#define INIT_CMD_NACK           254
#define INIT_CMD_ACK            255

struct init_request {
	int	magic;
	int	cmd;
	int	runlevel;
	int	sleeptime;
	char	data[368];
};

int is_finit_system (void);
int finit_register  (const char *dev);
int finit_handover  (const char *dev);


#endif /* WDOG_FINIT_H_ */
