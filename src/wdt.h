/* Advanced watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2020  Joachim Wiberg <troglobit@gmail.com>
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

#ifndef WDT_H_
#define WDT_H_

#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <paths.h>
#include <syslog.h>
#include <sched.h>

#ifdef _LIBITE_LITE
# include <libite/lite.h>
#else
# include <lite/lite.h>
#endif
#include <uev/uev.h>

#include "private.h"
#include "wdog.h"

#define WDT_DEVNODE          _PATH_DEV      "watchdog"
#define WDT_TIMEOUT_DEFAULT  20
#define WDT_KICK_DEFAULT     (WDT_TIMEOUT_DEFAULT / 2)

#define WDT_REASON_PID "PID                 "
#define WDT_REASON_WID "Watchdog ID         "
#define WDT_REASON_LBL "Label               "
#define WDT_RESET_DATE "Reset date          "
#define WDT_RESETCAUSE "Reset cause (WDIOF) "
#define WDT_REASON_STR "Reset reason        "
#define WDT_RESETCOUNT "Reset counter       "
#define WDT_TMOSEC_OPT "Timeout (sec)       "
#define WDT_INTSEC_OPT "Kick interval       "

#define EMERG(fmt, args...)  syslog(LOG_EMERG,   fmt, ##args)
#define ERROR(fmt, args...)  syslog(LOG_ERR,     fmt, ##args)
#define PERROR(fmt, args...) syslog(LOG_ERR,     fmt ": %s", ##args, strerror(errno))
#define DEBUG(fmt, args...)  syslog(LOG_DEBUG,   fmt, ##args)
#define LOG(fmt, args...)    syslog(LOG_NOTICE,  fmt, ##args)
#define INFO(fmt, args...)   syslog(LOG_INFO,    fmt, ##args)
#define WARN(fmt, args...)   syslog(LOG_WARNING, fmt, ##args)

/* Command line options */
extern char *opt_config;
extern int   opt_safe;
extern int   opt_timeout;
extern int   opt_interval;

/* Global variables */
extern int   magic;
extern int   enabled;
extern int   loglevel;
extern int   period;
extern int   timeout;
extern int   rebooting;
extern int   wait_reboot;
extern char *__progname;
#ifndef TESTMODE_DISABLED
extern int   __wdt_testmode;
#endif
extern unsigned int reset_counter;
extern wdog_reason_t reset_reason;

int wdt_init           (uev_ctx_t *ctx, const char *dev);
int wdt_exit           (uev_ctx_t *ctx);

int wdt_open           (const char *dev);
int wdt_close          (uev_ctx_t *ctx);

int wdt_capability     (uint32_t flag);

int wdt_enable         (int enable);
int wdt_debug          (int enable);

int wdt_kick           (const char *msg);
int wdt_set_timeout    (int count);
int wdt_get_timeout    (void);

int wdt_reset          (uev_ctx_t *ctx, pid_t pid, wdog_reason_t *reason, int timeout);
int wdt_forced_reset   (uev_ctx_t *ctx, pid_t pid, char *label, int timeout);

int wdt_fload_reason   (FILE *fp, wdog_reason_t *r, pid_t *pid);
int wdt_fstore_reason  (FILE *fp, wdog_reason_t *r, pid_t  pid);

int wdt_set_bootstatus (int timeout, int interval);
int wdt_get_bootstatus (void);

static inline unsigned int wdt_reset_counter(void)
{
	return reset_counter;
}

static inline int wdt_testmode(void)
{
#ifndef TESTMODE_DISABLED
	return __wdt_testmode;
#else
	return 0;
#endif
}

#endif /* WDT_H_ */

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
