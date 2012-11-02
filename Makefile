# A small userspace watchdog daemon
#
# Copyright (C) 2008 Michele d'Amico <michele.damico@fitre.it>
# Copyright (C) 2008 Mike Frysinger <vapier@gentoo.org>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
.PHONY: all clean romfs

# VERSION      ?= $(shell git tag -l | tail -1)
VERSION      ?= 1.2
EXEC          = watchdogd
OBJS          = watchdogd.o daemonize.o pidfile.o
CFLAGS       += -W -Wall -Werror
CPPFLAGS     += -D_GNU_SOURCE -DVERSION=\"$(VERSION)\"

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS$(LDLIBS_$@))

romfs: all
	$(ROMFSINST) /bin/$(EXEC)

clean:
	-@$(RM) $(EXEC) *.elf *.gdb *.o

distclean: clean

legal:
	-@printf "%-40s %s\n" $(PKG) "ISC License" >> $(LEGALDIR)/summary
	-@$(CP) LICENSE $(LEGALDIR)/$(PKG)

