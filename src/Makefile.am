AM_CFLAGS           = -W -Wall -Wextra -Wno-unused-parameter -std=gnu99
bin_PROGRAMS        = watchdogctl
sbin_PROGRAMS       = watchdogd
watchdogd_SOURCES   = watchdogd.c	private.h	\
		      wdt.c		wdt.h		\
		      api.c		api.h		\
		      conf.c		conf.h		\
		      finit.c		finit.h		\
		      rrfile.c		rr.h		\
		      script.c		script.h	\
		      supervisor.c	supervisor.h	\
					monitor.h

if FILENR_PLUGIN
watchdogd_SOURCES  += filenr.c
endif
if LOADAVG_PLUGIN
watchdogd_SOURCES  += loadavg.c
endif
if MEMINFO_PLUGIN
watchdogd_SOURCES  += meminfo.c
endif
if GENERIC_PLUGIN
watchdogd_SOURCES  += generic.c
endif

watchdogd_CPPFLAGS  = -D_GNU_SOURCE -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_XOPEN_SOURCE
watchdogd_CPPFLAGS += -DSYSCONFDIR=\"@sysconfdir@\"
watchdogd_CFLAGS    = $(uev_CFLAGS) $(lite_CFLAGS) $(confuse_CFLAGS) $(AM_CFLAGS)
watchdogd_LDADD     = $(uev_LIBS)   $(lite_LIBS) $(confuse_LIBS) libwdog.la

watchdogctl_SOURCES = watchdogctl.c
watchdogctl_CFLAGS  = $(lite_CFLAGS) $(AM_CFLAGS)
watchdogctl_LDADD   = $(lite_LIBS) libwdog.la

lib_LTLIBRARIES     = libwdog.la
pkgconfigdir        = $(libdir)/pkgconfig
pkgconfig_DATA      = libwdog.pc
pkgincludedir       = $(includedir)/wdog
pkginclude_HEADERS  =           wdog.h  compat.h
libwdog_la_SOURCES  = wdog.c	wdog.h  compat.h
libwdog_la_CFLAGS   = $(lite_CFLAGS) $(AM_CFLAGS)
libwdog_la_LDFLAGS  = -version-info 2:1:0

