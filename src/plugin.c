/* Plugin loader and plugin utility methods
 *
 * Copyright (C) 2016  Joachim Nilsson <troglobit@gmail.com>
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

#include "plugin.h"

/*
 * Initialize all plugins
 */
void wdt_plugins_init(uev_ctx_t *ctx, int T)
{
	/* Start process monitor */
	supervisor_init(ctx, T);
}

/*
 * Cleanup at exit
 */
void wdt_plugins_exit(uev_ctx_t *ctx)
{
	/* Shut down UNIX domain socket and restore priority */
	supervisor_exit(ctx);
}

/*
 * Plugins that follow wdt_enable()
 */
int wdt_plugins_enable(int enable)
{
	int result = 0;

	/* Stop/Start process monitor */
	result += supervisor_enable(enable);

	return result;
}

/*
 * Parse monitor plugin warning and critical level arg.
 */
int wdt_plugin_arg(char *desc, char *arg, double *warning, double *critical)
{
	char buf[16], *ptr;
	double value;

	if (!desc)
		desc = "unknown plugin";

	if (!arg) {
		ERROR("%s argument missing.", desc);
		return 1;
	}

	strlcpy(buf, arg, sizeof(buf));
	ptr = strchr(buf, ',');
	if (ptr)
		*ptr++ = 0;

	/* First argument is warning */
	value = strtod(buf, NULL);
	if (value <= 0) {
	error:
		ERROR("%s argument invalid or too small.", desc);
		return 1;
	}

	if (warning)
		*warning = value;

	/* Second argument, if given, is warning */
	if (ptr) {
		value = strtod(ptr, NULL);
		if (value <= 0)
			goto error;

		if (critical)
			*critical = value;
	}

	return 0;
}

/*
 * Concatenate __progname with plugin name for reset cause label
 */
char *wdt_plugin_label(char *plugin_name)
{
	static char name[16];

	snprintf(name, sizeof(name), "%s:%s", __progname, plugin_name);

	return name;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
