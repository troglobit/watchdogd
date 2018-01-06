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
#include "wdt.h"
#include "plugin.h"
#include "script.h"

static void checker(uev_ctx_t *ctx, cfg_t *cfg, const char *sect, int (*init)(uev_ctx_t *, int, int, float, float))
{
	cfg_t *sec;

	sec = cfg_getnsec(cfg, sect, 0);
	if (sec) {
		int period, logmark;
		float warn, crit;

		period  = cfg_getint(sec, "interval");
		logmark = cfg_getbool(sec, "logmark");
		warn    = cfg_getfloat(sec, "warning");
		crit    = cfg_getfloat(sec, "critical");
		init(ctx, period, logmark, warn, crit);
	} else {
		init(ctx, 0, 0, 0.0, 0.0);
	}
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
	cfg_opt_t checker_opts[] = {
		CFG_INT  ("interval", 300, CFGF_NONE),
		CFG_BOOL ("logmark",  cfg_false, CFGF_NONE),
		CFG_FLOAT("warning",  0.9, CFGF_NONE),
		CFG_FLOAT("critical", 0.95, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] = {
		CFG_INT ("interval",   WDT_KICK_DEFAULT, CFGF_NONE),
		CFG_INT ("timeout",    WDT_TIMEOUT_DEFAULT, CFGF_NONE),
		CFG_BOOL("safe-exit",  cfg_false, CFGF_NONE),
		CFG_STR ("script",     NULL, CFGF_NONE),
		CFG_SEC ("loadavg",    checker_opts, CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg;

	cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		PERROR("Failed initializing configuration file parser");
		return 1;
	}

	/* Custom logging, rather than default Confuse stderr logging */
	cfg_set_error_function(cfg, conf_errfunc);

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
	if (!opt_script)
		script_init(ctx, cfg_getstr(cfg, "script"));
	if (!opt_timeout)
		timeout = cfg_getint(cfg, "timeout");
	if (!opt_interval)
		period  = cfg_getint(cfg, "interval");

#ifdef LOADAVG_PERIOD
	checker(ctx, cfg, "loadavg", loadavg_init);
#endif

	return cfg_free(cfg);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
