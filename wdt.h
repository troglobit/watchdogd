/* A small userspace watchdog daemon
 *
 * Copyright (C) 2008       Michele d'Amico <michele.damico@fitre.it>
 * Copyright (C) 2008       Mike Frysinger <vapier@gentoo.org>
 * Copyright (C) 2012-2015  Joachim Nilsson <troglobit@gmail.com>
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

#include "lite/lite.h"
#include "uev/uev.h"

#define WDT_DEVNODE          "/dev/watchdog"
#define WDT_STATE            _PATH_VARDB "watchdogd.state"
#define WDT_TIMEOUT_DEFAULT  20
#define WDT_KICK_DEFAULT     (WDT_TIMEOUT_DEFAULT / 2)

#define ERROR(fmt, args...)                   syslog(LOG_ERR,   fmt, ##args)
#define PERROR(fmt, args...)                  syslog(LOG_ERR,   fmt ": %s", ##args, strerror(errno))
#define DEBUG(fmt, args...) do { if (verbose) syslog(LOG_DEBUG, fmt, ##args); } while(0)
#define INFO(fmt, args...)                    syslog(LOG_INFO,  fmt, ##args)
#define WARN(fmt, args...)                    syslog(LOG_WARNING, fmt, ##args)

/* Global variables */
extern int   magic;
extern int   enabled;
extern int   verbose;
extern int   sys_log;
extern int   extkick;
extern int   extdelay;
extern int   period;
extern char *__progname;
extern int   __wdog_testmode;

int wdt_enable         (int enable);

int wdt_kick           (char *msg);
int wdt_set_timeout    (int count);
int wdt_get_timeout    (void);
int wdt_get_bootstatus (void);
int wdt_close          (uev_ctx_t *ctx);
int wdt_reboot         (uev_ctx_t *ctx, pid_t pid, char *label);

int   wdt_plugin_arg   (char *desc, char *arg, double *warning, double *critical);
char *wdt_plugin_label (char *plugin_name);

#endif /* WDT_H_ */

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  version-control: t
 * End:
 */
