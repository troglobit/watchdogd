AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
bin_PROGRAMS        = watchdogctl
sbin_PROGRAMS       = watchdogd
dist_man8_MANS      = watchdogd.8
watchdogd_SOURCES   = watchdogd.c	wdt.h		\
		      plugin.c          plugin.h	\
		      filenr.c		filenr.h	\
		      loadavg.c		loadavg.h	\
		      meminfo.c		meminfo.h	\
		      pmon.c		pmon.h

watchdogd_CFLAGS    = $(uev_CFLAGS) $(lite_CFLAGS)
watchdogd_LDADD     = $(uev_LIBS)   $(lite_LIBS) libwdog.la

watchdogctl_SOURCES = watchdogctl.c
watchdogctl_LDADD   = libwdog.la

lib_LTLIBRARIES     = libwdog.la
pkgconfigdir        = $(libdir)/pkgconfig
pkgconfig_DATA      = libwdog.pc
pkgincludedir       = $(includedir)/wdog
pkginclude_HEADERS  =           wdog.h  compat.h
libwdog_la_SOURCES  = wdog.c	wdog.h  compat.h
libwdog_la_CFLAGS   = $(lite_CFLAGS)
libwdog_la_LDFLAGS  = -version-info 1:0:0

