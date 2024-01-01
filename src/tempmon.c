/* Temperature monitor
 *
 * Copyright (C) 2015-2023  Joachim Wiberg <troglobit@gmail.com>
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

#include <libgen.h>
#include <sys/statvfs.h>
#include "wdt.h"
#include "script.h"

#define HWMON_PATH    "/sys/class/hwmon/"
#define HWMON_NAME    "temp%d_label"
#define HWMON_TEMP    "temp%d_input"
#define HWMON_TRIP    "temp%d_crit"
#define THERMAL_PATH  "/sys/class/thermal/"
#define THERMAL_TRIP  "trip_point_0_temp"
#define TEMP_NEXTFILE WDOG_STATUSDIR ".tempmon.next"
#define TEMP_DUMPFILE WDOG_STATUSDIR "tempmon.json"

struct temp {
	TAILQ_ENTRY(temp) link; /* BSD sys/queue.h linked list node. */

	int    id;
	char   name[32];
	char  *temp;
	char  *crit;

	float  tcrit;
	float  tdata[10];
	int    tdpos;

	char  *exec;
	int    logmark;
	float  warning;
	float  critical;

	uev_t  watcher;
	int    interval;
	int    dirty;		/* for mark & sweep */
};

static TAILQ_HEAD(thead, temp) sensors = TAILQ_HEAD_INITIALIZER(sensors);

static uev_t filer;
static int   filer_active;


static char *paste(char *path, size_t len, char *file, size_t offset)
{
	if (offset >= len)
		return NULL;

	if (offset > 0)
		path[offset] = 0;
	strlcat(path, file, len);

	return path;
}

static char *read_file(const char *fn, char *buf, size_t len)
{
	char *ptr = NULL;
	FILE *fp;

	fp = fopen(fn, "r");
	if (fp) {
		if ((ptr = fgets(buf, len, fp)))
			chomp(buf);
		fclose(fp);
	}

	return ptr;
}

static void write_file(uev_t *w, void *arg, int events)
{
	char *fn = (char *)arg;
	struct temp *s;
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		PERROR("Failed writing to %s", fn);
		return;
	}

	fprintf(fp, "[\n");
	TAILQ_FOREACH(s, &sensors, link) {
		fprintf(fp, "  {\n");
		fprintf(fp, "    \"name\": \"%s\",\n", s->name);
		fprintf(fp, "    \"file\": \"%s\",\n", s->temp);
		fprintf(fp, "    \"critical\": \"%.1f\",\n", s->tcrit);
		fprintf(fp, "    \"temperature\": [ ");
		for (size_t i = 0; i < NELEMS(s->tdata); i++)
			fprintf(fp, "%s\"%.1f\"", i != 0 ? ", " : "", s->tdata[i]);
		fprintf(fp, " ],\n");
		fprintf(fp, "    \"interval\": %d\n", s->interval);
		fprintf(fp, "  }%s\n", TAILQ_NEXT(s, link) != TAILQ_END(&sensors) ? "," : "");
	}
	fprintf(fp, "]\n");

	fclose(fp);
	movefile(fn, TEMP_DUMPFILE);
}

static float read_temp(const char *path)
{
	float temp = 0.0;
	char buf[10];

	/* could be sensor with missing crit/max/trip, ignore */
	if (!path)
		return temp;

	DEBUG("Reading sensor %s", path);
	if (read_file(path, buf, sizeof(buf))) {
		const char *err;
		int tmp;

		DEBUG("Raw temp %s", buf);
		tmp  = strtonum(buf, -150000, 150000, &err);
		if (err) {
			DEBUG("Temperature reading %s, skipping ...", err);
		} else {
			temp = (float)tmp / 1000;
			DEBUG("Got temp %.1f°C", temp);
		}
	}

	return temp;
}

static float calc_mean(struct temp *sensor)
{
	size_t i, valid = 0, num = NELEMS(sensor->tdata);
	float  mean = 0.0;

	for (i = 0; i < num; i++) {
		float tdata = sensor->tdata[i];

		if (tdata != 0.0)
			valid++;
		mean += tdata;
	}

	return mean / valid;
}

static void cb(uev_t *w, void *arg, int events)
{
	struct temp *sensor = (struct temp *)arg;
	const char *nm = sensor->name;
	float temp, mean, trip, crit;

	sensor->tdata[sensor->tdpos++] = temp = read_temp(sensor->temp);
	trip = sensor->tcrit * sensor->warning;
	crit = sensor->tcrit * sensor->critical;
	mean = calc_mean(sensor);

	DEBUG("temp %.2f mean %.2f warning %.1f%% critical %.1f%% "
	      "tcrit %.2f trip %.2f crit %.2f",  temp, mean,
	      sensor->warning * 100, sensor->critical * 100,
	      sensor->tcrit, trip, crit);

	if (sensor->tdpos == NELEMS(sensor->tdata)) {
		sensor->tdpos = 0;

		LOG("%15s: current %.1f°C, mean %.1f°C, critical %.1f°C", nm, temp, mean, crit);
	}

	if (mean > trip) {
		if (crit != 0.0 && mean >= crit) {
			char label[48];

			EMERG("%s: temperature critical, %.1f >= %.1f!", nm, mean, crit);

			snprintf(label, sizeof(label), "%s:%s", PACKAGE, nm);
			if (checker_exec(sensor->exec, "tempmon", 1, mean, trip, crit))
				wdt_forced_reset(w->ctx, getpid(), label, 0);
			return;
		}

		WARN("%s: temperature warning, %.1f > %0.1f!", nm, mean, trip);
		checker_exec(sensor->exec, "tempmon", 0, mean, trip, crit);
	}

}

static int sanity_check(const char *path, float *temp)
{
	float tmp = read_temp(path);

	if (tmp == 0.0 || tmp < -150.0 || tmp > 150.0)
		return 1;

	if (temp)
		*temp = tmp;

	return 0;
}

static char *sensor_hwmon(struct temp *sensor, const char *temp, char *path, size_t len)
{
	size_t offset = strlen(path);
	char file[32];

	if (sscanf(&temp[offset], "temp%d_input", &sensor->id) != 1) {
		INFO("Failed reading ID from %s", temp);
		goto fail;
	}

	DEBUG("Got ID %d", sensor->id);
	if (sanity_check(temp, NULL)) {
		INFO("Improbable value detected, skipping %s", temp);
		goto fail;
	}

	snprintf(file, sizeof(file), HWMON_NAME, sensor->id);
	if (!read_file(paste(path, len, file, offset), sensor->name, sizeof(sensor->name)))
		read_file(paste(path, len, "name", offset), sensor->name, sizeof(sensor->name));

	snprintf(file, sizeof(file), HWMON_TRIP, sensor->id);
	if (fexist(paste(path, len, file, offset)))
		sensor->crit = path;

	if (!sensor->crit || sanity_check(sensor->crit, &sensor->tcrit)) {
	fail:
		sensor->tcrit = 100.0;
		sensor->crit = NULL;
		free(path);
	}

	return sensor->name[0] ? sensor->name : NULL;
}

static char *sensor_thermal(struct temp *sensor, const char *temp, char *path, size_t len)
{
	size_t offset = strlen(path);

	if (sscanf(temp, THERMAL_PATH "thermal_zone%d/temp", &sensor->id) != 1) {
		INFO("Failed reading ID from %s", temp);
		goto fail;
	}

	DEBUG("Got ID %d", sensor->id);
	if (sanity_check(temp, NULL)) {
		INFO("Improbable value detected, skipping %s", temp);
		goto fail;
	}

	read_file(paste(path, len, "type", offset), sensor->name, sizeof(sensor->name));

	if (fexist(paste(path, len, THERMAL_TRIP, offset)))
		sensor->crit = path;

	if (!sensor->crit || sanity_check(sensor->crit, &sensor->tcrit)) {
	fail:
		sensor->tcrit = 100.0;
		sensor->crit = NULL;
		free(path);
	}

	return sensor->name[0] ? sensor->name : NULL;
}

static char *identify(struct temp *sensor, const char *temp)
{
	size_t len = strlen(temp) + 32;
	char *path, *ptr;

	path = malloc(len);
	if (!path) {
		PERROR("Critical, cannot get memory for %s", temp);
		return NULL;
	}

	strlcpy(path, temp, len);
	ptr = rindex(path, '/');
	if (!ptr)
		goto fail;
	*(++ptr) = 0;

	DEBUG("Base path %s len %zd", path, len);
	if (!strncmp(path, HWMON_PATH, strlen(HWMON_PATH)))
		return sensor_hwmon(sensor, temp, path, len);
	else if (!strncmp(path, THERMAL_PATH, strlen(THERMAL_PATH)))
		return sensor_thermal(sensor, temp, path, len);

fail:
	ERROR("This does not look like a temp sensor %s", temp);
	free(path);

	return NULL;
}

static struct temp *find(const char *name)
{
	struct temp *sensor;

	TAILQ_FOREACH(sensor, &sensors, link) {
		if (strcmp(sensor->name, name))
			continue;

		return sensor;
	}

	return NULL;
}

void tempmon_mark(void)
{
	struct temp *sensor;

	TAILQ_FOREACH(sensor, &sensors, link) {
		sensor->dirty = 1;
	}
}

void tempmon_sweep(void)
{
	struct temp *sensor, *tmp;

	TAILQ_FOREACH_SAFE(sensor, &sensors, link, tmp) {
		if (!sensor->dirty)
			continue;

		TAILQ_REMOVE(&sensors, sensor, link);
		uev_timer_stop(&sensor->watcher);
		free(sensor->name);
		free(sensor);
	}

	if (TAILQ_EMPTY(&sensors)) {
		INFO("Temperature monitor disabled.");
		uev_timer_stop(&filer);
	} else
		uev_timer_start(&filer);
}

int tempmon_init(uev_ctx_t *ctx, const char *path, int T, int mark,
	       float warn, float crit, char *script)
{
	struct temp *sensor;

	if (!path) {
		if (T)
			ERROR("temp monitor missing path to sensor input");
		return 1;
	}

	sensor = find(path);
	if (!sensor) {
		if (!fexist(path)) {
			ERROR("Missing sensor %s, skipping", path);
			return 1;
		}

		sensor = calloc(1, sizeof(*sensor));
		if (!sensor) {
		fail:
			PERROR("failed creating sensor monitor %s", path);
			return 1;
		}

		sensor->temp = strdup(path);
		if (!sensor->temp) {
			free(sensor);
			goto fail;
		}

		if (!identify(sensor, path)) {
			ERROR("Cannot find sensor %s, skipping.", sensor->temp);
			free(sensor->temp);
			free(sensor);
			return 1;
		}

		TAILQ_INSERT_TAIL(&sensors, sensor, link);
	} else {
		sensor->dirty = 0;
		if (!T) {
			INFO("Sensor monitor %s disabled.", sensor->name);
			return uev_timer_stop(&sensor->watcher);
		}
	}

	INFO("Sensor monitor: %s period %d sec, warning: %.1f%%, reboot: %.1f%%",
	     path, T, warn * 100, crit * 100);

	sensor->logmark = mark;
	sensor->warning = warn;
	sensor->critical = crit;
	sensor->interval = T;
	if (script) {
		if (sensor->exec)
			free(sensor->exec);
		sensor->exec = strdup(script);
	}

	uev_timer_stop(&sensor->watcher);
	if (!filer_active) {
		uev_timer_init(ctx, &filer, write_file, TEMP_NEXTFILE, 100, 5000);
		filer_active = 1;
	}

	return uev_timer_init(ctx, &sensor->watcher, cb, sensor, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
