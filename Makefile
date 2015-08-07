# A small userspace watchdog daemon
#
# Copyright (C) 2008 Michele d'Amico <michele.damico@fitre.it>
# Copyright (C) 2008 Mike Frysinger <vapier@gentoo.org>
# Copyright (C) 2012 Joachim Nilsson <troglobit@gmail.com>
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
.PHONY: all install clean distclean dist

# Top directory for building complete system, fall back to this directory
ROOTDIR    ?= $(shell pwd)

# VERSION      ?= $(shell git tag -l | tail -1)
VERSION    ?= 1.6-dev
NAME        = watchdogd
PKG         = $(NAME)-$(VERSION)
ARCHIVE     = $(PKG).tar
ARCHIVEZ    = ../$(ARCHIVE).xz
EXEC        = $(NAME)
DISTFILES   = LICENSE README
OBJS        = watchdogd.o loadavg.o
SRCS        = $(OBJS:.o=.c)
DEPS        = $(SRCS:.c=.d)

CFLAGS     += -W -Wall -Werror
CPPFLAGS   += -D_GNU_SOURCE -D_DEFAULT_SOURCE -DVERSION=\"$(VERSION)\"
LDLIBS     += libite/libite.a

# Installation paths, always prepended with DESTDIR if set
prefix     ?= /usr
sbindir    ?= /sbin
datadir     = $(prefix)/share/doc/$(NAME)

include common.mk

all: $(LDLIBS) $(EXEC)

$(LDLIBS): Makefile
	+@$(MAKE) STATIC=1 -C `dirname $@` all

$(EXEC): $(OBJS) $(LDLIBS)

install: all
	@$(INSTALL) -d $(DESTDIR)$(datadir)
	@$(INSTALL) -d $(DESTDIR)$(sbindir)
	@for file in $(DISTFILES); do	                                \
		printf "  INSTALL $(DESTDIR)$(datadir)/$$file\n";	\
		$(INSTALL) -m 0644 $$file $(DESTDIR)$(datadir)/$$file;	\
	done
	@printf "  INSTALL $(DESTDIR)$(sbindir)/$(EXEC)\n"
	$(STRIPINST) $(EXEC) $(DESTDIR)$(sbindir)/$(EXEC)

uninstall:
	-@for file in $(DISTFILES); do	                                \
		printf "  REMOVE  $(DESTDIR)$(datadir)/$$file\n";	\
		rm $(DESTDIR)$(datadir)/$$file 2>/dev/null;		\
	done
	@printf "  REMOVE  $(DESTDIR)$(sbindir)/$(EXEC)\n"
	-@$(RM) $(DESTDIR)$(sbindir)/$(EXEC) 2>/dev/null
	-@rmdir $(DESTDIR)$(datadir) 2>/dev/null
	-@rmdir $(DESTDIR)$(sbindir) 2>/dev/null

clean:
	+@$(MAKE) -C libite $@
	-@$(RM) $(OBJS) $(DEPS) $(EXEC)

distclean: clean
	+@$(MAKE) -C libite $@
	-@$(RM) $(JUNK) unittest *.elf *.gdb *.o .*.d

dist:
	@if [ x"$(ARCHTOOL)" = x"" ]; then \
		echo "Missing git-archive-all from https://github.com/Kentzo/git-archive-all"; \
		exit 1; \
	fi
	@if [ -e $(ARCHIVEZ) ]; then \
		echo "Distribution already exists."; \
		exit 1; \
	fi
	@echo "Building xz tarball of $(PKG) in parent dir..."
	@$(ARCHTOOL) ../$(ARCHIVE)
	@xz ../$(ARCHIVE)
	@md5sum $(ARCHIVEZ) | tee $(ARCHIVEZ).md5

dev: distclean
	@echo "Building unstable xz $(DEV) in parent dir..."
	-@$(RM) -f ../$(DEV).tar.xz*
	@(dir=`mktemp -d`; mkdir $$dir/$(DEV); cp -a . $$dir/$(DEV); \
	  cd $$dir; tar --exclude=.git -c -J -f $(DEV).tar.xz $(DEV);\
	  cd - >/dev/null; mv $$dir/$(DEV).tar.xz ../; cd ..;        \
	  rm -rf $$dir; md5sum $(DEV).tar.xz | tee $(DEV).tar.xz.md5)

-include $(DEPS)
