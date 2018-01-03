/* Customer compatibility wrapper API
 *
 * Copyright (c) 2016  Joachim Nilsson <joachim.nilsson@westermo.se>
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

#include <unistd.h>
#include <signal.h>		/* kill(), SIGINT */
#include <sys/reboot.h>		/* reboot(), RB_AUTOBOOT */

static inline int wdog_is_enabled(void)
{
	int status = 0;

	wdog_status(&status);
	return status;
}

static inline int wdog_debug(int enable)
{
	return wdog_set_debug(enable);
}

static inline void wdog_forced_reset(char *label)
{
	if (!label || !label[0])
		label = "XBAD_LABEL";

	if (wdog_reboot(getpid(), label)) {
		/*
		 * Fallback handling in case API fails, the user expects
		 * a reboot now.  Try an orderly reboot first simulating
		 * ctrl-alt-del from the kernel, then do kernel reboot.
		 */
		if (kill(1, SIGINT))
			reboot(RB_AUTOBOOT);
	}
}

static inline int wdog_get_reason(wdog_reason_t *reason)
{
	return wdog_reboot_reason(reason);
}

static inline int wdog_get_reason_raw(wdog_reason_t *reason)
{
	return wdog_reboot_reason_raw(reason);
}

static inline char *wdog_get_reason_str(wdog_reason_t *reason)
{
	return wdog_reboot_reason_str(reason);
}

static inline int wdog_clear_reason(void)
{
	return wdog_reboot_reason_clr();
}

static inline int wdog_pmon_ping(void)
{
	return wdog_ping();
}

/* Returns ID or -errno */
static inline int wdog_pmon_subscribe(char *label, int timeout, int *ack)
{
	return wdog_subscribe(label, (unsigned int)timeout, (unsigned int *)ack);
}

/* Returns 0 if OK, or errno */
static inline int wdog_pmon_unsubscribe(int id, int ack)
{
	return wdog_unsubscribe(id, (unsigned int)ack);
}

/* Returns 0 while OK, or errno */
static inline int wdog_pmon_kick(int id, int *ack)
{
	return wdog_kick2(id, (unsigned int *)ack);
}

static inline int wdog_pmon_extend_kick(int id, int timeout, int *ack)
{
	return wdog_extend_kick(id, (unsigned int)timeout, (unsigned int *)ack);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
