#ifndef WATCHDOGD_SCRIPT_H_
#define WATCHDOGD_SCRIPT_H_

int script_init (uev_ctx_t *ctx, char *script);
int script_exec (char *exec, char *nm, int iscrit, double val, double warn, double crit);

#endif /* WATCHDOGD_SCRIPT_H_ */
