AC_INIT(watchdogd, 3.2-beta1, https://github.com/troglobit/watchdogd/issues)
AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz subdir-objects])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/watchdogd.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile watchdogd.service src/libwdog.pc src/Makefile examples/Makefile man/Makefile])

# Older versions of autoconf (<2.58) do not have AC_CONFIG_MACRO_DIR()
#m4_include([m4/local-macros.m4])
AC_CONFIG_MACRO_DIR([m4])

# Change default prefix, we're usually needed on the first tape
AC_PREFIX_DEFAULT([])

# Checks for programs.
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL

# Check for header files
AC_HEADER_STDC
AC_CHECK_HEADERS([finit/finit.h], [finit=yes], [finit=no])

# Check for required library versions
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([uev],  [libuev >= 2.1.0])
PKG_CHECK_MODULES([lite], [libite >= 2.0.1])
PKG_CHECK_MODULES([confuse], [libconfuse >= 3.0])

# Check for configure switches
AC_ARG_ENABLE([compat],
	[AS_HELP_STRING([--enable-compat], [Enable compat supervisor.status and signals])],,
	[enable_compat=no], [])
AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples], [Build process supervisor client examples])],
	[enable_examples=yes], [])
AC_ARG_ENABLE([loadavg],
	[AS_HELP_STRING([--enable-loadavg], [Enable CPU load average monitor])],
	[enable_loadavg=$enableval], [enable_loadavg=no])
AC_ARG_ENABLE([filenr],
	[AS_HELP_STRING([--enable-filenr], [Enable file descriptor leak monitor])],
	[enable_filenr=$enableval], [enable_filenr=no])
AC_ARG_ENABLE([meminfo],
	[AS_HELP_STRING([--enable-meminfo], [Enable memory leak monitor])],
	[enable_meminfo=$enableval], [enable_meminfo=no])
AC_ARG_ENABLE([generic],
	[AS_HELP_STRING([--enable-generic], [Enable generic script monitor])],
	[enable_generic=$enableval], [enable_generic=no])
AC_ARG_ENABLE([syslog-mark],
	[AS_HELP_STRING([--enable-syslog-mark], [Periodic syslog *MARK* by monitor plugins])],
	[enable_syslog_mark=yes], [])

AC_ARG_ENABLE(test-mode,
        AS_HELP_STRING([--disable-test-mode], [Disable test mode]))
AC_ARG_ENABLE(builtin-tests,
        AS_HELP_STRING([--disable-builtin-tests], [Disable process supervisor built-in tests]))
AC_ARG_ENABLE(rcfile,
        AS_HELP_STRING([--disable-rcfile], [Disable file store reset cause backend]))

AC_ARG_WITH([systemd],
	[AS_HELP_STRING([--with-systemd=DIR], [Directory for systemd service files, default: auto])],,
	[with_systemd=auto])

AS_IF([test "x$enable_compat" = "xyes"], [
	AC_DEFINE(COMPAT_SUPERVISOR,  1, [Enable compat /run/supervisor.status and signals])])

AS_IF([test "x$enable_loadavg" != "xno"], [
	AS_IF([test "x$enable_loadavg" = "xyes"], [enable_loadavg=300])
	AC_DEFINE_UNQUOTED(LOADAVG_PLUGIN, $enable_loadavg, [Enable CPU load average monitor])])

AS_IF([test "x$enable_filenr" != "xno"], [
	AS_IF([test "x$enable_filenr" = "xyes"], [enable_filenr=300])
	AC_DEFINE_UNQUOTED(FILENR_PLUGIN, $enable_filenr, [Enable file descriptor leak monitor])])

AS_IF([test "x$enable_meminfo" != "xno"], [
	AS_IF([test "x$enable_meminfo" = "xyes"], [enable_meminfo=300])
	AC_DEFINE_UNQUOTED(MEMINFO_PLUGIN, $enable_meminfo, [Enable memory leak monitor])])

AS_IF([test "x$enable_generic" != "xno"], [
	AS_IF([test "x$enable_generic" = "xyes"], [enable_generic=300])
	AC_DEFINE_UNQUOTED(GENERIC_PLUGIN, $enable_generic, [Enable generic script monitor])])

AS_IF([test "x$enable_syslog_mark" = "xyes"], [
	AC_DEFINE(SYSLOG_MARK, 1, [Enable periodic syslog MARK by monitor plugins])])

AS_IF([test "x$enable_test_mode" != "xno"], enable_test_mode="yes",[
	AC_DEFINE(TESTMODE_DISABLED,  1, [Disable test mode])])

AS_IF([test "x$enable_builtin_tests" != "xno"], enable_builtin_tests="yes",[
	AC_DEFINE(SUPERVISOR_TESTS_DISABLED,  1, [Disable process supervisor tests in watchdogctl])])

AS_IF([test "x$enable_rcfile" != "xno"], enable_rcfile="yes",[
	AC_DEFINE(RCFILE_DISABLED,  1, [Disable reset cause file store backend])])

# Check where to install the systemd .service file
AS_IF([test "x$with_systemd" = "xyes" -o "x$with_systemd" = "xauto"], [
     def_systemd=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
     AS_IF([test "x$def_systemd" = "x"],
         [AS_IF([test "x$with_systemd" = "xyes"],
	     [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
	     with_systemd=no], [with_systemd="$def_systemd"])]
)
AS_IF([test "x$with_systemd" != "xno"],
     [AC_SUBST([systemddir], [$with_systemd])])

AM_CONDITIONAL([HAVE_SYSTEMD], [test "x$with_systemd" != "xno"])
AM_CONDITIONAL(LOADAVG_PLUGIN, [test "x$enable_loadavg" != "xno"])
AM_CONDITIONAL(FILENR_PLUGIN, [test "x$enable_filenr" != "xno"])
AM_CONDITIONAL(MEMINFO_PLUGIN, [test "x$enable_meminfo" != "xno"])
AM_CONDITIONAL(GENERIC_PLUGIN, [test "x$enable_generic" != "xno"])
AM_CONDITIONAL(FINIT, [test "x$finit" = "xyes"])
AM_CONDITIONAL(RCFILE, [test "x$enable_rcfile" = "xyes"])
AM_CONDITIONAL(ENABLE_EXAMPLES, [test "$enable_examples" = yes])

# Expand $sbindir early, into $SBINDIR, for systemd unit file
# NOTE: This does *not* take prefix/exec_prefix override at "make
#       install" into account, unfortunately.
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
DOCDIR=`eval echo $docdir`
DOCDIR=`eval echo $DOCDIR`
AC_SUBST(DOCDIR)
SBINDIR=`eval echo $sbindir`
SBINDIR=`eval echo $SBINDIR`
AC_SUBST(SBINDIR)

# Generate all files
AC_OUTPUT
