ACLOCAL_AMFLAGS     = -I m4

sbin_PROGRAMS       = watchdogd
dist_man8_MANS      = watchdogd.8
pkgconfigdir        = $(libdir)/pkgconfig
pkgconfig_DATA      = libwdog.pc
doc_DATA            = README.md LICENSE
EXTRA_DIST          = README.md LICENSE ChangeLog.md
DISTCLEANFILES      = *~ DEADJOE semantic.cache *.gdb *.elf core core.* *.d *.sock

watchdogd_SOURCES   = watchdogd.c	wdt.h		\
		      plugin.c          plugin.h	\
		      filenr.c		filenr.h	\
		      loadavg.c		loadavg.h	\
		      meminfo.c		meminfo.h	\
		      pmon.c		pmon.h

watchdogd_CFLAGS    = -W -Wall -Wextra -std=gnu99 $(uev_CFLAGS) $(lite_CFLAGS)
watchdogd_LDADD     = libwdog.la $(uev_LIBS) $(lite_LIBS)

lib_LTLIBRARIES     = libwdog.la
pkgincludedir       = $(includedir)/wdog
pkginclude_HEADERS  =           wdog.h  compat.h
libwdog_la_SOURCES  = wdog.c	wdog.h  compat.h
libwdog_la_CFLAGS   = -W -Wall -Wextra -std=gnu99 $(lite_CFLAGS)
libwdog_la_LDFLAGS  = $(AM_LDFLAGS) -version-info 1:0:0

DIST_SUBDIRS        = examples
SUBDIRS             = .
if ENABLE_EXAMPLES
SUBDIRS            += examples
endif

## Generate MD5 checksum file
MD5 = md5sum
md5-dist:
	@for file in $(DIST_ARCHIVES); do	\
		$(MD5) $$file > $$file.md5;	\
	done

## Check if tagged in git
release-hook:
	if [ ! `git tag | grep $(PACKAGE_VERSION)` ]; then				\
		echo;									\
		printf "\e[1m\e[41mCannot find release tag $(PACKAGE_VERSION)\e[0m\n";	\
		printf "\e[1m\e[5mDo release anyway?\e[0m "; read yorn;			\
		if [ "$$yorn" != "y" -a "$$yorn" != "Y" ]; then				\
			printf "OK, aborting release.\n";				\
			exit 1;								\
		fi;									\
		echo;									\
	else										\
		echo;									\
		printf "\e[1m\e[42mFound GIT release tag $(PACKAGE_VERSION)\e[0m\n";	\
		printf "\e[1m\e[44m>>Remember to push tags!\e[0m\n";			\
		echo;									\
	fi

## Target to run when building a release
release: dist release-hook md5-dist
	@for file in $(DIST_ARCHIVES); do						\
		printf "$$file    \tDistribution tarball\n";				\
		printf "$$file.md5\t"; cat $$file.md5 | cut -f1 -d' ';			\
		mv $$file* ../;								\
	done

