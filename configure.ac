AC_INIT(watchdogd, 3.0-dev, https://github.com/troglobit/watchdogd/issues)
AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz subdir-objects])
LT_INIT([dlopen aix-soname=both disable-fast-install])

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([watchdogd.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile examples/Makefile])

# Older versions of autoconf (<2.58) do not have AC_CONFIG_MACRO_DIR()
#m4_include([m4/local-macros.m4])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_HEADER_STDC
AC_PROG_LN_S
AC_PROG_INSTALL

# Check for required libraries
PKG_CHECK_MODULES([uev],  [libuev >= 1.4.0])
PKG_CHECK_MODULES([lite], [libite >= 1.5.0])

# Generate all files
AC_OUTPUT
