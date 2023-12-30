/* .conf file parser
 *
 * Copyright (C) 2018-2023  Joachim Wiberg <troglobit@gmail.com>
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

#include <confuse.h>
#include <libgen.h>
#include <sched.h>

#include "wdt.h"
#include "rr.h"
#include "script.h"
#include "monitor.h"
#include "supervisor.h"

static char *fn;

#if defined(FILENR_PLUGIN) || defined(FSMON_PLUGIN) || defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(TEMPMON_PLUGIN)
static int checker(uev_ctx_t *ctx, cfg_t *cfg, const char *sect,
		   int (*init)(uev_ctx_t *, const char *, int, int, float, float, char *))
{
	unsigned int i, num;
	int rc = 0;

	num = cfg_size(cfg, sect);
	if (!num) {
		/* Disable checker completely */
		return 0;
	}

	for (i = 0; i < num; i++) {
		cfg_t *sec = cfg_getnsec(cfg, sect, i);
		const char *name = cfg_title(sec);
		char arg[(name ? strlen(name) : 0) + 2];
		int enabled, period, logmark;
		float warn, crit;
		char *script;

		if (!sec)
			continue;

		enabled = cfg_getbool(sec, "enabled");
		period  = cfg_getint(sec, "interval");
		logmark = cfg_getbool(sec, "logmark");
		warn    = cfg_getfloat(sec, "warning");
		crit    = cfg_getfloat(sec, "critical");
		script  = cfg_getstr(sec, "script");

		if (name)
			snprintf(arg, sizeof(arg), " %s", name);
		else
			arg[0] = 0;

		rc += init(ctx, name, enabled ? period : 0, logmark, warn, crit, script);
	}

	return rc;
}
#endif

#if defined(GENERIC_PLUGIN)
static int generic_checker(uev_ctx_t *ctx, cfg_t *cfg)
{
	unsigned int i, num;
	int rc = 0;

	num = cfg_size(cfg, "generic");
	if (!num) {
		/* Disable checker completely */
		return 0;
	}

	for (i = 0; i < num; i++) {
		cfg_t *sec = cfg_getnsec(cfg, "generic", i);
		int enabled, period, timeout;
		int warn_level, crit_level;
		const char *monitor;
		char *script;

		if (!sec)
			continue;

		enabled    = cfg_getbool(sec, "enabled");
		period     = cfg_getint(sec, "interval");
		timeout    = cfg_getint(sec, "timeout");
		warn_level = cfg_getint(sec, "warning");
		crit_level = cfg_getint(sec, "critical");
		script     = cfg_getstr(sec, "script");
		monitor    = cfg_title(sec);
		if (!monitor)
			monitor = cfg_getstr(sec, "monitor-script");
		if (!monitor) {
			ERROR("generic script, missing monitor script path.");
			continue;
		}

		rc += generic_init(ctx, monitor, enabled ? period : 0, timeout, warn_level, crit_level, script);
	}

	return rc;
}
#endif

static int validate_reset_reason(uev_ctx_t *ctx, cfg_t *cfg)
{
	if (!cfg)
		return reset_reason_init(0, NULL);

	return reset_reason_init(cfg_getbool(cfg, "enabled"), cfg_getstr(cfg, "file"));
}

static int validate_file(cfg_t *cfg, cfg_opt_t *opt)
{
	int rc = -1;
	char *file, *dir, *tmp;

	file = cfg_getstr(cfg, opt->name);
	if (!file)
		return 0;

	tmp = strdup(file);
	if (!tmp) {
		cfg_error(cfg, "failed allocating memory");
		return -1;
	}

	dir = dirname(tmp);
	if (file[0] != '/' || !dir) {
		cfg_error(cfg, "reset-reason backend file must be an absolute path, skipping.");
		goto done;
	}

	if (access(dir, R_OK | W_OK)) {
		cfg_error(cfg, "reset-reason dir '%s' not writable, error %d:%s.", dir, errno, strerror(errno));
		goto done;
	}

	rc = 0;
done:
	free(tmp);
	return rc;
}

static int supervisor(uev_ctx_t *ctx, cfg_t *cfg)
{
	char *script;
	int enabled, prio;

	if (!cfg)
		return supervisor_init(ctx, 0, 0, NULL);

	enabled = cfg_getbool(cfg, "enabled");
	prio    = cfg_getint(cfg, "priority");
	script  = cfg_getstr(cfg, "script");

	return supervisor_init(ctx, enabled, prio, script);
}

static int validate_priority(cfg_t *cfg, cfg_opt_t *opt)
{
	const char *errstr = NULL;
	long long val = cfg_getint(cfg, opt->name);
	long long pmin = sched_get_priority_min(SCHED_RR);
	long long pmax = sched_get_priority_max(SCHED_RR);

	if (val < pmin)
		errstr = "too small";
	if (val > pmax)
		errstr = "too large";

	if (errstr) {
		cfg_error(cfg, "supervisor priority '%lld' is %s!", val, errstr);
		return -1;
	}

	return 0;
}

static void conf_errfunc(cfg_t *cfg, const char *format, va_list args)
{
	char fmt[80];

	if (!cfg) {
		vsyslog(LOG_ERR, format, args);
		return;
	}

	if (cfg->line)
		snprintf(fmt, sizeof(fmt), "%s:%d: %s", fn, cfg->line, format);
	else
		snprintf(fmt, sizeof(fmt), "%s: %s", fn, format);

	vsyslog(LOG_ERR, fmt, args);
}

int conf_parse_file(uev_ctx_t *ctx, char *file)
{
	cfg_opt_t supervisor_opts[] =  {
		CFG_BOOL("enabled",  cfg_false, CFGF_NONE),
		CFG_INT ("priority", 0, CFGF_NONE),
		CFG_STR ("script",   NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t reset_reason_opts[] =  {
		CFG_BOOL("enabled",  cfg_false, CFGF_NONE),
		CFG_STR ("file",     NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t checker_opts[] = {
		CFG_BOOL ("enabled",  cfg_false, CFGF_NONE),
		CFG_INT  ("interval", 300, CFGF_NONE),
		CFG_BOOL ("logmark",  cfg_false, CFGF_NONE),
		CFG_FLOAT("warning",  0.9, CFGF_NONE),
		CFG_FLOAT("critical", 0.0, CFGF_NONE), /* Disabled by default */
		CFG_STR  ("script",   NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t generic_opts[] = {
		CFG_BOOL ("enabled",        cfg_false, CFGF_NONE),
		CFG_INT  ("interval",       300, CFGF_NONE),
		CFG_INT  ("timeout",        300, CFGF_NONE),
		CFG_BOOL ("logmark",        cfg_false, CFGF_NONE),
		CFG_INT  ("warning",        1, CFGF_NONE),
		CFG_INT  ("critical",       2, CFGF_NONE),
		CFG_STR  ("monitor-script", NULL, CFGF_NONE),
		CFG_STR  ("script",         NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] = {
		CFG_INT ("interval",    WDT_KICK_DEFAULT, CFGF_NONE),
		CFG_INT ("timeout",     WDT_TIMEOUT_DEFAULT, CFGF_NONE),
		CFG_BOOL("safe-exit",   cfg_false, CFGF_NONE),
		CFG_SEC ("supervisor",  supervisor_opts, CFGF_NONE),
		CFG_SEC ("reset-cause", reset_reason_opts, CFGF_NONE), /* Compat only */
		CFG_SEC ("reset-reason", reset_reason_opts, CFGF_NONE),
		CFG_STR ("script",      NULL, CFGF_NONE),
		CFG_SEC ("filenr",      checker_opts, CFGF_NONE),
		CFG_SEC ("fsmon",       checker_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_SEC ("generic",     generic_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_SEC ("loadavg",     checker_opts, CFGF_NONE),
		CFG_SEC ("meminfo",     checker_opts, CFGF_NONE),
		CFG_SEC ("temp",        checker_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_END()
	};
	cfg_t *cfg, *opt;

	if (!ctx) {
		ERROR("Internal error, no event context");
		return 1;
	}

	if (!file)
		return 1;

	if (!fexist(file)) {
		WARN("Configuration file %s does not exist", file);
		return 1;
	}

	cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		PERROR("Failed initializing configuration file parser");
		return 1;
	}

	/* Custom logging, rather than default Confuse stderr logging */
	fn = file;
	cfg_set_error_function(cfg, conf_errfunc);

	/* Validators */
	cfg_set_validate_func(cfg, "supervisor|priority", validate_priority);
	cfg_set_validate_func(cfg, "reset-cause|file", validate_file); /* Compat only */
	cfg_set_validate_func(cfg, "reset-reason|file", validate_file);

	switch (cfg_parse(cfg, file)) {
	case CFG_FILE_ERROR:
		ERROR("Cannot read configuration file %s", file);
		return 1;

	case CFG_PARSE_ERROR:
		ERROR("Parse error in %s", file);
		return 1;

	case CFG_SUCCESS:
		break;
	}

	/* Read settings, command line options take precedence */
	if (!opt_safe)
		magic = cfg_getbool(cfg, "safe-exit");
	if (!opt_timeout)
		timeout = cfg_getint(cfg, "timeout");
	if (!opt_interval)
		period  = cfg_getint(cfg, "interval");

	script_init(ctx, cfg_getstr(cfg, "script"));
	supervisor(ctx, cfg_getnsec(cfg, "supervisor", 0));
	opt = cfg_getnsec(cfg, "reset-reason", 0);
	if (!opt)
		opt = cfg_getnsec(cfg, "reset-cause", 0); /* Compat only */
	validate_reset_reason(ctx, opt);

#ifdef FILENR_PLUGIN
	checker(ctx, cfg, "filenr", filenr_init);
#endif
#ifdef FSMON_PLUGIN
	fsmon_mark();
	checker(ctx, cfg, "fsmon", fsmon_init);
	fsmon_sweep();
#endif
#ifdef GENERIC_PLUGIN
	generic_mark();
	generic_checker(ctx, cfg);
	generic_sweep();
#endif
#ifdef LOADAVG_PLUGIN
	checker(ctx, cfg, "loadavg", loadavg_init);
#endif
#ifdef MEMINFO_PLUGIN
	checker(ctx, cfg, "meminfo", meminfo_init);
#endif
#ifdef TEMPMON_PLUGIN
	checker(ctx, cfg, "temp", temp_init);
#endif

	return cfg_free(cfg);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
