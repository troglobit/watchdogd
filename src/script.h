#ifndef WATCHDOGD_SCRIPT_H_
#define WATCHDOGD_SCRIPT_H_

int   script_init     (uev_ctx_t *ctx, char *script);

int   checker_exec    (char *exec, char *nm, int iscrit, double val, double warn, double crit);
pid_t supervisor_exec (char *exec, int c, int p, char *label);
pid_t generic_exec    (char *exec, int warn, int crit);

int   exit_code       (pid_t pid);

#endif /* WATCHDOGD_SCRIPT_H_ */
