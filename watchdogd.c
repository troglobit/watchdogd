#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <paths.h>

int fd = -1;

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
void keep_alive(void)
{
	int dummy;

	ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

void safe_exit()
{
	if (fd != -1) {
		write(fd, "V", 1);
		close(fd);
	}
	exit(0);
}

int set_wd_counter(int count)
{
	return ioctl(fd, WDIOC_SETTIMEOUT, &count);
}

int get_wd_counter()
{
	int count;
	int err;
	if ((err = ioctl(fd, WDIOC_GETTIMEOUT, &count))) {
		count = err;
	}
	return count;
}

#define FOREGROUND_FLAG "-f"

#define _PATH_DEVNULL "/dev/null"

void vfork_daemon_rexec(int nochdir, int noclose, int argc, char **argv, char *foreground_opt)
{
	int f;
	char **vfork_args;
	int a = 0;

	setsid();

	if (!nochdir)
		chdir("/");

	if (!noclose && (f = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		dup2(f, STDIN_FILENO);
		dup2(f, STDOUT_FILENO);
		dup2(f, STDERR_FILENO);
		if (f > 2)
			close(f);
	}

	vfork_args = malloc(sizeof(char *) * (argc + 2));
	while (*argv) {
		vfork_args[a++] = *argv;
		argv++;
	}
	vfork_args[a++] = foreground_opt;
	vfork_args[a++] = NULL;
	switch (vfork()) {
	case 0:		/* child */
		/* Make certain we are not a session leader, or else we
		 * might reacquire a controlling terminal */
		if (vfork())
			_exit(0);
		execvp(vfork_args[0], vfork_args);
		perror("execv");
		exit(-1);
	case -1:		/* error */
		perror("vfork");
		exit(-1);
	default:		/* parent */
		exit(0);
	}
}

static void usage(char *argv[])
{
	printf(
		"%s [-f] [-w <sec>] [-k <sec>] [-s] [-h|--help]\n"
		"A simple watchdog deamon that send WDIOC_KEEPALIVE ioctl every some\n"
		"\"heartbeat of keepalives\" seconds.\n"
		"Options:\n"
		"\t-f        start in foreground (background is default)\n"
		"\t-w <sec>  set the watchdog counter to <sec> in seconds\n"
		"\t-k <sec>  set the \"heartbeat of keepalives\" to <sec> in seconds\n"
		"\t-s        safe exit (disable Watchdog) for CTRL-c and kill -SIGTERM signals\n"
		"\t--help|-h write this help message and exit\n",
		argv[0]);
}

/*
 * The main program.
 */
int main(int argc, char *argv[])
{
	int wd_count = 20;
	int real_wd_count = 0;
	int wd_keep_alive = wd_count / 2;
	struct sigaction sa;
	int background = 1;
	int ac = argc;
	char **av = argv;

	memset(&sa, 0, sizeof(sa));

	/* TODO: rewrite this to use getopt() */
	while (--ac) {
		++av;
		if (strcmp(*av, "-w") == 0) {
			if (--ac) {
				wd_count = atoi(*++av);
				printf("-w switch: set watchdog counter to %d sec.\n", wd_count);
			} else {
				fprintf(stderr, "-w switch must be followed to seconds of watchdog counter.\n");
				fflush(stderr);
				break;
			}
		} else if (strcmp(*av, "-k") == 0) {
			if (--ac) {
				wd_keep_alive = atoi(*++av);
				printf("-k switch: set the heartbeat of keepalives in %d sec.\n", wd_keep_alive);
			} else {
				fprintf(stderr, "-k switch must be followed to seconds of heartbeat of keepalives.\n");
				fflush(stderr);
				break;
			}
		} else if (strcmp(*av, "-s") == 0) {
			printf("-s switch: safe exit (CTRL-C and kill).\n");
			sa.sa_handler = safe_exit;
			sigaction(SIGINT, &sa, NULL);
			sigaction(SIGTERM, &sa, NULL);
		} else if (strcmp(*av, FOREGROUND_FLAG) == 0) {
			background = 0;
			printf("Start in foreground mode.\n");
		} else if ((strcmp(*av, "-h") == 0) || (strcmp(*av, "--help") == 0)) {
			usage(argv);
			exit(0);
		} else {
			fprintf(stderr, "Unrecognized option \"%s\".\n", *av);
			usage(argv);
			exit(1);
		}
	}

	if (background) {
		printf("Start in deamon mode.\n");
		vfork_daemon_rexec(1, 0, argc, argv, FOREGROUND_FLAG);
	}

	fd = open("/dev/watchdog", O_WRONLY);

	if (fd == -1) {
		perror("Watchdog device not enabled");
		fflush(stderr);
		exit(-1);
	}

	if (set_wd_counter(wd_count)) {
		fprintf(stderr, "-w switch: wrong value. Please look at kernel log for more dettails.\n Continue with the old value\n");
		fflush(stderr);
	}

	real_wd_count = get_wd_counter();
	if (real_wd_count < 0) {
		perror("Error while issue IOCTL WDIOC_GETTIMEOUT");
	} else {
		if (real_wd_count <= wd_keep_alive) {
			fprintf(stderr,
				"Warning watchdog counter less or equal to the heartbeat of keepalives: %d <= %d\n",
				real_wd_count, wd_keep_alive);
			fflush(stderr);
		}
	}

	while (1) {
		keep_alive();
		sleep(wd_keep_alive);
	}
}
