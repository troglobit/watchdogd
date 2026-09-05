/* Linux PSI memory pressure parser
 *
 * Copyright (C) 2026  The watchdogd authors
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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pressure-parser.h"

#define PSI_LINE_MAX 256
#define PSI_SCOPE_SOME 0x01
#define PSI_SCOPE_FULL 0x02

static int parse_number(char *str, double *value)
{
	char *end;

	errno = 0;
	*value = strtod(str, &end);
	if (errno == ERANGE || end == str || *end || !isfinite(*value) ||
	    *value < 0.0 || *value > 100.0)
		return -1;

	return 0;
}

static int parse_total(char *str)
{
	char *end;

	if (*str == '-')
		return -1;

	errno = 0;
	(void)strtoull(str, &end, 10);
	if (errno == ERANGE || end == str || *end)
		return -1;

	return 0;
}

static int parse_scope(const char *name)
{
	if (!strcmp(name, "some"))
		return PRESSURE_SOME;
	if (!strcmp(name, "full"))
		return PRESSURE_FULL;

	return -1;
}

static int parse_window(const char *name)
{
	if (!strcmp(name, "avg10"))
		return PRESSURE_AVG10;
	if (!strcmp(name, "avg60"))
		return PRESSURE_AVG60;
	if (!strcmp(name, "avg300"))
		return PRESSURE_AVG300;

	return -1;
}

int pressure_scope_parse(const char *name, enum pressure_scope *scope)
{
	int value;

	if (!name || !scope) {
		errno = EINVAL;
		return -1;
	}

	value = parse_scope(name);
	if (value < 0) {
		errno = EINVAL;
		return -1;
	}

	*scope = value;
	return 0;
}

int pressure_window_parse(const char *name, enum pressure_window *window)
{
	int value;

	if (!name || !window) {
		errno = EINVAL;
		return -1;
	}

	value = parse_window(name);
	if (value < 0) {
		errno = EINVAL;
		return -1;
	}

	*window = value;
	return 0;
}

int pressure_read(const char *path, struct pressure_stats *stats)
{
	FILE *fp;
	char buf[PSI_LINE_MAX];
	unsigned int seen = 0;
	int saved_errno = 0;

	if (!path || !stats) {
		errno = EINVAL;
		return -1;
	}

	memset(stats, 0, sizeof(*stats));
	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (fgets(buf, sizeof(buf), fp)) {
		char *save, *token;
		double *values;
		unsigned int scope_bit;
		int scope;
		int have[4] = { 0, 0, 0, 0 };

		token = strtok_r(buf, " \t\r\n", &save);
		if (!token)
			continue;

		scope = parse_scope(token);
		if (scope < 0) {
			saved_errno = EINVAL;
			goto fail;
		}

		values = scope == PRESSURE_SOME ? stats->some : stats->full;
		scope_bit = scope == PRESSURE_SOME ? PSI_SCOPE_SOME : PSI_SCOPE_FULL;
		if (seen & scope_bit) {
			saved_errno = EINVAL;
			goto fail;
		}

		while ((token = strtok_r(NULL, " \t\r\n", &save))) {
			char *equals = strchr(token, '=');
			char *number;
			double value;
			int index = -1;

			if (!equals || equals == token || !equals[1]) {
				saved_errno = EINVAL;
				goto fail;
			}

			*equals = '\0';
			number = equals + 1;
			if (!strcmp(token, "avg10"))
				index = PRESSURE_AVG10;
			else if (!strcmp(token, "avg60"))
				index = PRESSURE_AVG60;
			else if (!strcmp(token, "avg300"))
				index = PRESSURE_AVG300;
			else if (!strcmp(token, "total"))
				index = 3;

			/* Keep accepting future PSI fields, but validate known values. */
			if (index < 0)
				continue;
			if (have[index] || (index == 3 ? parse_total(number) :
						 parse_number(number, &value))) {
				saved_errno = EINVAL;
				goto fail;
			}

			if (index < 3)
				values[index] = value;
			have[index] = 1;
		}

		if (!have[PRESSURE_AVG10] || !have[PRESSURE_AVG60] ||
		    !have[PRESSURE_AVG300] || !have[3]) {
			saved_errno = EINVAL;
			goto fail;
		}

		seen |= scope_bit;
	}

	if (ferror(fp))
		saved_errno = errno ? errno : EIO;
	else if (seen != (PSI_SCOPE_SOME | PSI_SCOPE_FULL))
		saved_errno = EINVAL;

	if (saved_errno)
		goto fail;

	fclose(fp);
	return 0;

fail:
	if (!saved_errno)
		saved_errno = EINVAL;
	fclose(fp);
	errno = saved_errno;
	return -1;
}

double pressure_value(const struct pressure_stats *stats,
		      enum pressure_scope scope, enum pressure_window window)
{
	if (!stats || scope < PRESSURE_SOME || scope > PRESSURE_FULL ||
	    window < PRESSURE_AVG10 || window > PRESSURE_AVG300)
		return -1.0;

	return scope == PRESSURE_SOME ? stats->some[window] : stats->full[window];
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
