/* .conf file parser
 *
 * Copyright (C) 2018  Joachim Nilsson <troglobit@gmail.com>
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
#include "rc.h"
#include "script.h"
#include "filenr.h"
#include "loadavg.h"
#include "meminfo.h"
#include "supervisor.h"


#if defined(LOADAVG_PLUGIN) || defined(MEMINFO_PLUGIN) || defined(FILENR_PLUGIN)
static int checker(uev_ctx_t *ctx, cfg_t *cfg, const char *sect,
		   int (*init)(uev_ctx_t *, int, int, float, float, char *))
{
	int rc;
	cfg_t *sec;

	sec = cfg_getnsec(cfg, sect, 0);
	if (sec) {
		int period, logmark;
		char *script;
		float warn, crit;

		period  = cfg_getint(sec, "interval");
		logmark = cfg_getbool(sec, "logmark");
		warn    = cfg_getfloat(sec, "warning");
		crit    = cfg_getfloat(sec, "critical");
		script  = cfg_getstr(sec, "script");
		rc = init(ctx, period, logmark, warn, crit, script);
	} else {
		rc = init(ctx, 0, 0, 0.0, 0.0, NULL);
	}

	return rc;
}
#endif

static int reset_cause(uev_ctx_t *ctx, cfg_t *cfg)
{
	if (!cfg)
		return reset_cause_init(0, NULL);

	return reset_cause_init(cfg_getbool(cfg, "enabled"), cfg_getstr(cfg, "file"));
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
		cfg_error(cfg, "reset-cause file must be an absolute path, skipping.");
		goto done;
	}

	if (access(dir, R_OK | W_OK)) {
		cfg_error(cfg, "reset-cause dir '%s' not writable, error %d:%s.", dir, errno, strerror(errno));
		goto done;
	}

	rc = 0;
done:
	free(tmp);
	return rc;
}

static int supervisor(uev_ctx_t *ctx, cfg_t *cfg)
{
	if (!cfg)
		return supervisor_init(ctx, 0, 0);

	return supervisor_init(ctx, cfg_getbool(cfg, "enabled"), cfg_getint(cfg, "priority"));
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

	if (cfg && cfg->filename && cfg->line)
		snprintf(fmt, sizeof(fmt), "%s:%d: %s", cfg->filename, cfg->line, format);
	else if (cfg && cfg->filename)
		snprintf(fmt, sizeof(fmt), "%s: %s", cfg->filename, format);
	else
		snprintf(fmt, sizeof(fmt), "%s", format);

	vsyslog(LOG_ERR, fmt, args);
}

int conf_parse_file(uev_ctx_t *ctx, char *file)
{
	cfg_opt_t supervisor_opts[] =  {
		CFG_BOOL("enabled",  cfg_false, CFGF_NONE),
		CFG_INT ("priority", 98, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t reset_cause_opts[] =  {
		CFG_BOOL("enabled",  cfg_false, CFGF_NONE),
		CFG_STR ("file", NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t checker_opts[] = {
		CFG_INT  ("interval", 300, CFGF_NONE),
		CFG_BOOL ("logmark",  cfg_false, CFGF_NONE),
		CFG_FLOAT("warning",  0.9, CFGF_NONE),
		CFG_FLOAT("critical", 0.0, CFGF_NONE), /* Disabled by default */
		CFG_STR  ("script",   NULL, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] = {
		CFG_INT ("interval",    WDT_KICK_DEFAULT, CFGF_NONE),
		CFG_INT ("timeout",     WDT_TIMEOUT_DEFAULT, CFGF_NONE),
		CFG_BOOL("safe-exit",   cfg_false, CFGF_NONE),
		CFG_SEC ("supervisor",  supervisor_opts, CFGF_NONE),
		CFG_SEC ("reset-cause", reset_cause_opts, CFGF_NONE),
		CFG_STR ("script",      NULL, CFGF_NONE),
		CFG_SEC ("filenr",      checker_opts, CFGF_NONE),
		CFG_SEC ("loadavg",     checker_opts, CFGF_NONE),
		CFG_SEC ("meminfo",     checker_opts, CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg;

	if (!ctx) {
		ERROR("Internal error, no event context");
		return 1;
	}

	if (!file || !fexist(file))
		return 1;

	cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		PERROR("Failed initializing configuration file parser");
		return 1;
	}

	/* Custom logging, rather than default Confuse stderr logging */
	cfg_set_error_function(cfg, conf_errfunc);

	/* Validators */
	cfg_set_validate_func(cfg, "supervisor|priority", validate_priority);
	cfg_set_validate_func(cfg, "reset-cause|file", validate_file);

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
	reset_cause(ctx, cfg_getnsec(cfg, "reset-cause", 0));

#ifdef FILENR_PLUGIN
	checker(ctx, cfg, "filenr", filenr_init);
#endif
#ifdef LOADAVG_PLUGIN
	checker(ctx, cfg, "loadavg", loadavg_init);
#endif
#ifdef MEMINFO_PLUGIN
	checker(ctx, cfg, "meminfo", meminfo_init);
#endif

	return cfg_free(cfg);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
