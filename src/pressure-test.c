/* Host-side tests for the Linux PSI parser. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pressure-parser.h"

static char *fixture(const char *contents)
{
	char path[] = "/tmp/watchdogd-pressure-XXXXXX";
	int fd;
	FILE *fp;

	fd = mkstemp(path);
	assert(fd >= 0);
	fp = fdopen(fd, "w");
	assert(fp);
	assert(fputs(contents, fp) >= 0);
	assert(fclose(fp) == 0);

	return strdup(path);
}

static void remove_fixture(char *path)
{
	assert(unlink(path) == 0);
	free(path);
}

int main(void)
{
	static const char valid[] =
		"some avg10=1.25 avg60=2.50 avg300=3.75 total=123\n"
		"full avg10=0.25 avg60=0.50 avg300=0.75 total=45\n";
	static const char duplicate[] =
		"some avg10=1 avg10=2 avg60=3 avg300=4 total=5\n"
		"full avg10=1 avg60=2 avg300=3 total=4\n";
	static const char missing[] =
		"some avg10=1 avg60=2 avg300=3 total=4\n";
	static const char malformed[] =
		"some avg10=nan avg60=2 avg300=3 total=4\n"
		"full avg10=1 avg60=2 avg300=3 total=4\n";
	static const char out_of_range[] =
		"some avg10=101 avg60=2 avg300=3 total=4\n"
		"full avg10=1 avg60=2 avg300=3 total=4\n";
	static const char bad_total[] =
		"some avg10=1 avg60=2 avg300=3 total=nope\n"
		"full avg10=1 avg60=2 avg300=3 total=4\n";
	static const char negative_total[] =
		"some avg10=1 avg60=2 avg300=3 total=-1\n"
		"full avg10=1 avg60=2 avg300=3 total=4\n";
	struct pressure_stats stats;
	char *path;
	enum pressure_scope scope;
	enum pressure_window window;

	path = fixture(valid);
	assert(pressure_read(path, &stats) == 0);
	assert(stats.some[PRESSURE_AVG10] == 1.25);
	assert(stats.full[PRESSURE_AVG300] == 0.75);
	assert(pressure_scope_parse("full", &scope) == 0);
	assert(scope == PRESSURE_FULL);
	assert(pressure_window_parse("avg60", &window) == 0);
	assert(window == PRESSURE_AVG60);
	assert(pressure_value(&stats, scope, window) == 0.50);
	assert(pressure_scope_parse("cpu", &scope) < 0);
	assert(pressure_window_parse("avg5", &window) < 0);
	remove_fixture(path);

	path = fixture(duplicate);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	path = fixture(missing);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	path = fixture(malformed);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	path = fixture(out_of_range);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	path = fixture(bad_total);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	path = fixture(negative_total);
	assert(pressure_read(path, &stats) < 0);
	remove_fixture(path);

	assert(pressure_read("/this/path/does/not/exist", &stats) < 0);
	puts("pressure parser tests: OK");
	return 0;
}
