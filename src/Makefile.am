AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
bin_PROGRAMS        = watchdogctl
sbin_PROGRAMS       = watchdogd

watchdogd_SOURCES   = watchdogd.c	wdt.h		\
		      plugin.c          plugin.h	\
		      filenr.c		filenr.h	\
		      loadavg.c		loadavg.h	\
		      meminfo.c		meminfo.h	\
		      pmon.c		pmon.h

watchdogd_CFLAGS    = $(uev_CFLAGS) $(lite_CFLAGS)
watchdogd_LDADD     = libwdog.la $(uev_LIBS) $(lite_LIBS)

watchdogctl_SOURCES = watchdogctl.c
watchdogctl_LDADD   = libwdog.la

lib_LTLIBRARIES     = libwdog.la
pkgincludedir       = $(includedir)/wdog
pkginclude_HEADERS  =           wdog.h  compat.h
libwdog_la_SOURCES  = wdog.c	wdog.h  compat.h
libwdog_la_CFLAGS   = -W -Wall -Wextra -std=gnu99 $(lite_CFLAGS)
libwdog_la_LDFLAGS  = $(AM_LDFLAGS) -version-info 1:0:0

