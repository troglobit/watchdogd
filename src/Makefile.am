AM_CFLAGS           = -W -Wall -Wextra -Wno-unused-parameter -std=gnu99
AM_CPPFLAGS         = -DSYSCONFDIR=\"@sysconfdir@\"
bin_PROGRAMS        = watchdogctl
sbin_PROGRAMS       = watchdogd
watchdogd_SOURCES   = watchdogd.c	wdt.h		\
		      api.c		api.h		\
		      conf.c		conf.h		\
		      plugin.c          plugin.h	\
		      script.c		script.h	\
		      supervisor.c	supervisor.h
if FINIT
watchdogd_SOURCES  += finit.c
endif
if FILENR_PLUGIN
watchdogd_SOURCES  += filenr.c		filenr.h
endif
if LOADAVG_PLUGIN
watchdogd_SOURCES  += loadavg.c		loadavg.h
endif
if MEMINFO_PLUGIN
watchdogd_SOURCES  += meminfo.c		meminfo.h
endif
if RCFILE
watchdogd_SOURCES  += rcfile.c
endif

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
libwdog_la_LDFLAGS  = -version-info 1:0:0

