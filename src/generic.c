/* Generic script monitor
 *
 * Copyright (C) 2015       Christian Lockley <clockley1@gmail.com>
 * Copyright (C) 2015-2018  Joachim Nilsson <troglobit@gmail.com>
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

#include <sys/wait.h>
#include <unistd.h>

#include "wdt.h"
#include "script.h"

static uev_t watcher;

typedef struct generic_script {  
    int is_running;
    pid_t pid;
    int warning;
    int critical;
    char *monitor_script;
    char *exec;
} generic_script_t;


static void wait_for_generic_script(uev_t *w, void *arg, int events)
{
	int status;
    pid_t pid = 1;
    generic_script_t* script_args;
    script_args = (generic_script_t*) arg;

    INFO("Monitor Script (PID %d): Got SIGCHLD so checking exit code, events: %d", script_args->pid, events);
	while ((pid = waitpid(script_args->pid, &status, WNOHANG)) > 0) {

		status = WEXITSTATUS(status);
        if (status >= script_args->critical) 
        {
            ERROR("Monitor Script (PID %d) returned exit status above critical treshold: %d, rebooting system ...", pid, status);
            if (checker_exec(script_args->exec, "generic", 1, status, script_args->warning, script_args->critical))
                wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
        } 
        else if(status >= script_args->warning)
        {
			WARN("Monitor Script (PID %d) returned exit status above warning treshold: %d", pid, status);
            checker_exec(script_args->exec, "generic", 0, status, script_args->warning, script_args->critical);
        }
        else 
        {
            INFO("Monitor script ran OK");
        }
        break;
	}
    INFO("Sigchild callback done");
}

static int run_generic_script(uev_t *w, generic_script_t* script_args) 
{
    pid_t pid;    
    pid = fork();
    if (!pid) {
        char value[5];
        char *argv[] = {
            script_args->monitor_script,
            NULL,
            NULL,
        };
        snprintf(value, sizeof(value), "%d", script_args->warning);
        argv[1] = value;
        snprintf(value, sizeof(value), "%d", script_args->critical);
        argv[2] = value;
        _exit(execv(argv[0], argv));
    }
    if (pid < 0) {
        ERROR("Cannot start script %s", script_args->monitor_script);
        return -1;
    }
    INFO("Started generic monitor script %s with PID %d", script_args->monitor_script, pid);
    uev_signal_init(w->ctx, &watcher, wait_for_generic_script, script_args, SIGCHLD);
    return pid;
}

static void cb(uev_t *w, void *arg, int events)
{
    generic_script_t* script_args;
    script_args = (generic_script_t*) arg;
    if (!script_args) {
        ERROR("Oops, no args?");
		return;
    }
    
    if(!script_args->is_running) {
        INFO("Starting the generic monitor script");
        
        script_args->pid = run_generic_script(w, script_args);
        if(script_args->pid > 0) {
            script_args->is_running = 1;
        }
    }
    else 
    {
        ERROR("Timeout reached and the script %s is still running, rebooting system ...", script_args->monitor_script);
        if (checker_exec(script_args->exec, "generic", 1, 100, script_args->warning, script_args->critical))
            wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
        return;
    }
}

/*
 * Every T seconds we run the given script
 * If it returns nonzero or runs for more than timeout we are critical
 */
int generic_init(uev_ctx_t *ctx, int T, int timeout, char *monitor, int mark, int warn, int crit, char *script)
{
	if (!T) {
		INFO("Generic script monitor disabled.");
		return uev_timer_stop(&watcher);
	}

    if (!monitor) {
		ERROR("Generic script monitor not started, please provide script-monitor.");
		return uev_timer_stop(&watcher);
	}
    
	INFO("Generic script monitor, period %d sec, max timeout: %d, monitor script: %s, warning level: %d, critical level: %d", T, timeout, monitor, warn, crit);

	uev_timer_stop(&watcher);
    //todo: get any old args and free them + free their monitor_script and exec fields
    
    generic_script_t* script_args;
    script_args = (generic_script_t*) malloc(sizeof (generic_script_t));
    INFO("OK 1");
    if(script_args) {
        script_args->is_running = 0;
        script_args->pid = -1;
        script_args->warning = warn;
        script_args->critical = crit;    
        INFO("OK 2");
        script_args->monitor_script = strdup(monitor);
        INFO("OK 3");
        script_args->exec = NULL;
        if (script) 
        {
            script_args->exec = strdup(script);            
        }
        
        INFO("OK 4");
    }
    INFO("OK 5");
	return uev_timer_init(ctx, &watcher, cb, script_args, timeout * 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
