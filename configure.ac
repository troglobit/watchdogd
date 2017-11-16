AC_INIT(watchdogd, 3.0-beta1, https://github.com/troglobit/watchdogd/issues)
AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz subdir-objects])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/watchdogd.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/libwdog.pc src/Makefile examples/Makefile man/Makefile])

# Older versions of autoconf (<2.58) do not have AC_CONFIG_MACRO_DIR()
#m4_include([m4/local-macros.m4])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL

# Check for header files
AC_HEADER_STDC
AC_CHECK_HEADERS([finit/finit.h])

# Check for required library versions
PKG_CHECK_MODULES([uev],  [libuev >= 2.1.0])
PKG_CHECK_MODULES([lite], [libite >= 1.5.0])

# Check for configure switches
AC_ARG_ENABLE([compat],
	[AS_HELP_STRING([--enable-compat], [Enable compat supervisor.status])],,
	[enable_compat=no], [])

AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples], [Build PMON client examples])],
	[enable_examples=yes], [])
AM_CONDITIONAL([ENABLE_EXAMPLES], [test "$enable_examples" = yes])

AC_ARG_ENABLE(test-mode,
        AS_HELP_STRING([--disable-test-mode], [Disable test mode]))
AC_ARG_ENABLE(builtin-tests,
        AS_HELP_STRING([--disable-builtin-tests], [Disable built-in PMON tests]))

AC_ARG_ENABLE(rcfile,
        AS_HELP_STRING([--disable-rcfile], [Disable file store reset cause backend]))

AS_IF([test "x$enable_compat" = "xyes"], [
	AC_DEFINE(COMPAT_SUPERVISOR,  1, [Enable compat /run/supervisor.status])])

AS_IF([test "x$enable_test_mode" != "xno"], enable_test_mode="yes",[
	AC_DEFINE(TESTMODE_DISABLED,  1, [Disable test mode])])

AS_IF([test "x$enable_builtin_tests" != "xno"], enable_builtin_tests="yes",[
	AC_DEFINE(PMON_TESTS_DISABLED,  1, [Disable PMON tests in watchdogctl])])

AS_IF([test "x$enable_rcfile" != "xno"], enable_rcfile="yes",[
	AC_DEFINE(RCFILE_DISABLED,  1, [Disable reset cause file store backend])])

AM_CONDITIONAL(RCFILE, [test "x$enable_rcfile" = "xyes"])

# Generate all files
AC_OUTPUT
