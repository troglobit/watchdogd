ACLOCAL_AMFLAGS     = -I m4

sbin_PROGRAMS       = watchdogd
dist_man8_MANS      = watchdogd.8
doc_DATA            = README.md LICENSE
EXTRA_DIST          = README.md LICENSE ChangeLog.md

watchdogd_SOURCES   = watchdogd.c	wdt.h		\
		      filenr.c		filenr.h	\
		      loadavg.c		loadavg.h	\
		      meminfo.c		meminfo.h	\
		      pmon.c		pmon.h

watchdogd_CFLAGS    = -W -Wall -Wextra -std=gnu99 $(uev_CFLAGS) $(lite_CFLAGS)
watchdogd_CPPFLAGS  = -D_GNU_SOURCE -D_DEFAULT_SOURCE
watchdogd_LDADD     = libwdog.la $(uev_LIBS) $(lite_LIBS)

lib_LTLIBRARIES     = libwdog.la
pkgincludedir       = $(includedir)/wdog
pkginclude_HEADERS  = wdog.h
libwdog_la_SOURCES  = wdog.c	wdog.h
libwdog_la_CFLAGS   = -W -Wall -Wextra -std=gnu99
libwdog_la_LDFLAGS  = $(AM_LDFLAGS) -version-info 1:0:0


