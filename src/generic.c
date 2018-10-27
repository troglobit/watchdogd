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
#include <signal.h>

#include "wdt.h"
#include "script.h"

typedef struct generic_script {
    uev_t watcher;
    int is_init;
    int is_running;
    pid_t pid;
    int warning;
    int critical;
    uev_t monitor_script_watcher;
    char *monitor_script;
    char *exec;
} generic_script_t;

static generic_script_t* single_monitor_script = NULL;

static void wait_for_generic_script(uev_t *w, void *arg, int events)
{
	int status;
    pid_t pid = 1;
    generic_script_t* script_args;
    script_args = (generic_script_t*)arg;
    INFO("Monitor Script (PID %d): verifying if still running, events: %d", script_args->pid, events);
	if ((pid = waitpid(script_args->pid, &status, WNOHANG)) > 0) {

        if(pid == script_args->pid) {
            uev_timer_stop(&script_args->monitor_script_watcher);
            script_args->is_running = 0;
            
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
            
        }
        else
        {
            INFO("Monitor script waitpid %d and script is %d", pid, script_args->pid);    
        }
	}
    else
    {        
        INFO("Monitor script still running: waitpid: %d  errno: %d", pid, errno);
    }
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
            0
        };
        snprintf(value, sizeof(value), "%d", script_args->warning);
        argv[1] = value;
        snprintf(value, sizeof(value), "%d", script_args->critical);
        argv[2] = value;
        sigset_t sigs;
        sigfillset(&sigs);
        sigprocmask(SIG_UNBLOCK, &sigs, 0);
        _exit(execv(argv[0], argv));
    }
    if (pid < 0) {
        ERROR("Cannot start script %s", script_args->monitor_script);
        return -1;
    }
    INFO("Started generic monitor script %s with PID %d", script_args->monitor_script, pid);
    script_args->pid = pid;
    script_args->is_running = 1;
    
    uev_timer_stop(&script_args->monitor_script_watcher);
    uev_timer_init(w->ctx, &script_args->monitor_script_watcher, wait_for_generic_script, script_args, 1000, 1000);    
        
    return pid;
}

static void cb(uev_t *w, void *arg, int events)
{
    generic_script_t* script_args;
    script_args = (generic_script_t*)arg;    
    if (!script_args) {
        ERROR("Oops, no args?");
		return;
    }
    
    if(!script_args->is_running) {
        INFO("Starting the generic monitor script");
        
        if(run_generic_script(w, script_args) <= 0)
        {
            if (script_args->critical > 0) 
            {
                ERROR("Could not start the monitor script %s, rebooting system ...", script_args->monitor_script);
                if (checker_exec(script_args->exec, "generic", 1, 100, script_args->warning, script_args->critical))
                    wdt_forced_reset(w->ctx, getpid(), PACKAGE ":generic", 0);
            }
            else 
            {
                WARN("Could not start the monitor script %s, but is not critical", script_args->monitor_script);
            }
            return;
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

static void stop_and_cleanup(generic_script_t* script) 
{
    if(script) {
        uev_timer_stop(&script->monitor_script_watcher);
        uev_timer_stop(&script->watcher);
        if(script->exec) {
            free(script->exec);
        }
        if(script->monitor_script) {
            free(script->monitor_script);
        }
        free(script);
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
        stop_and_cleanup(single_monitor_script);
        single_monitor_script = NULL;
        return 0;
	}

    if (!monitor) {
		ERROR("Generic script monitor not started, please provide script-monitor.");
        stop_and_cleanup(single_monitor_script);
        single_monitor_script = NULL;
        return 0;
	}
    
	INFO("Generic script monitor, period %d sec, max timeout: %d, monitor script: %s, warning level: %d, critical level: %d", T, timeout, monitor, warn, crit);

    stop_and_cleanup(single_monitor_script);
        
    single_monitor_script = (generic_script_t*) malloc(sizeof (generic_script_t));
    if(single_monitor_script) {
        single_monitor_script->is_running = 0;
        single_monitor_script->is_init = 0;
        single_monitor_script->pid = -1;
        single_monitor_script->warning = warn;
        single_monitor_script->critical = crit;    
        single_monitor_script->monitor_script = strdup(monitor);
        single_monitor_script->exec = NULL;
        if (script) 
        {
            single_monitor_script->exec = strdup(script);            
        }
        INFO("Start monitor timer");
        
        return uev_timer_init(ctx, &single_monitor_script->watcher, cb, single_monitor_script, T * 1000, T * 1000);
    }
    return 0;
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
