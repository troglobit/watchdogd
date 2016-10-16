AC_INIT(watchdogd, 3.0-beta1, https://github.com/troglobit/watchdogd/issues)
AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz subdir-objects])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([watchdogd.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile libwdog.pc examples/Makefile])

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

# Check for configure switches
AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples], [build examples])],
	[enable_examples=yes], [])
AM_CONDITIONAL([ENABLE_EXAMPLES], [test "$enable_examples" = yes])

AC_ARG_ENABLE(test-mode,
        AS_HELP_STRING([--disable-test-mode], [Disable test mode, for extra safety, default: enabled]))
AS_IF([test "x$enable_test_mode" != "xno"], enable_test_mode="yes",[
	AC_DEFINE(TESTMODE_DISABLED,  1, [Disable test mode])])

# Generate all files
AC_OUTPUT
