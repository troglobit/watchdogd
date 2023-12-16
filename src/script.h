/* Run script as monitor plugin callback
 *
 * Copyright (c) 2017-2020  Joachim Wiberg <troglobit@gmail.com>
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

#ifndef WATCHDOGD_SCRIPT_H_
#define WATCHDOGD_SCRIPT_H_

int   script_init        (uev_ctx_t *ctx, char *script);
int   script_exit_status (pid_t pid);

int   checker_exec       (char *exec, char *nm, int iscrit, double val, double warn, double crit);
pid_t supervisor_exec    (char *exec, int c, int p, char *label, void (*cb)(void *), void *arg);
pid_t generic_exec       (char *exec, int warn, int crit);

#endif /* WATCHDOGD_SCRIPT_H_ */
