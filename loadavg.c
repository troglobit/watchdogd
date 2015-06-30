#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <sys/sysinfo.h>

int get_cpu_count(void)
{
	struct sysinfo info;
	if (sysinfo(&info) < 0) {
		return 1;
	}
	return info.procs;
}

double check_loadavg(void)
{
	double load[3] = { 0 };
	if (getloadavg(load, 3) == -1) {
		return -1;
	}
	double avg_load = (load[0] + load[1]) / 2;
	return avg_load;
}
