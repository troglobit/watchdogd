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

static inline int wdog_forced_reset(char *label)
{
	return wdog_reboot(getpid(), label);
}

static inline int wdog_get_reason(wdog_reason_t *reason)
{
	return wdog_reboot_reason(reason);
}

static inline char *wdog_get_reason_str(wdog_reason_t *reason)
{
	return wdog_reboot_reason_str(reason);
}

static inline int wdog_clear_reason(void)
{
	return wdog_reboot_reason_clr();
}

static inline int wdog_subscribe(char *label, unsigned int timeout, unsigned int *next_ack)
{
        return wdog_pmon_subscribe(label, (int)timeout, (int *)next_ack);
}

static inline int wdog_unsubscribe(int wid, unsigned int ack)
{
        return wdog_pmon_unsubscribe(wid, (int)ack);
}

static inline int wdog_kick(int wid, unsigned int timeout, unsigned int ack, unsigned int *next_ack)
{
        int result, new_ack = (int)ack;

        result = wdog_pmon_extend_kick(wid, (int)timeout, &new_ack);
        if (!result)
                *next_ack = (unsigned int)new_ack;

        return result;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
