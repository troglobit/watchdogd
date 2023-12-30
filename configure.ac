AC_INIT([watchdogd], [4.0-dev], [https://github.com/troglobit/watchdogd/issues],,
	[https://troglobit.com/watchdogd.html])
AC_CONFIG_AUX_DIR(aux)
AM_INIT_AUTOMAKE([1.11 foreign dist-xz subdir-objects])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/watchdogd.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile
	watchdogd.service
	doc/Makefile
	src/libwdog.pc
	src/Makefile
	examples/Makefile
	man/Makefile])

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
AC_CHECK_HEADERS([sys/ioctl.h uev/uev.h libite/lite.h lite/lite.h])

# Check for required library versions
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([uev],  [libuev >= 2.1.0])
PKG_CHECK_MODULES([lite], [libite >= 2.0.1])
PKG_CHECK_MODULES([confuse], [libconfuse >= 3.0])

# Check for configure switches
AC_ARG_ENABLE([compat],
	[AS_HELP_STRING([--enable-compat], [Enable compat supervisor.status and signals])],,
	[enable_compat=no])

AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples], [Build process supervisor client examples])],,
	[enable_examples=no])

AC_ARG_ENABLE(test-mode,
        AS_HELP_STRING([--disable-test-mode], [Disable test mode]))

AC_ARG_ENABLE(builtin-tests,
        AS_HELP_STRING([--disable-builtin-tests], [Disable process supervisor built-in tests]))

AC_ARG_WITH([filenr],
	AS_HELP_STRING([--with-filenr], [Enable file descriptor leak monitor]),
	[with_filenr=$withval], [with_filenr=no])

AC_ARG_WITH([fsmon],
	AS_HELP_STRING([--with-fsmon], [Enable file system monitor]),
	[with_fsmon=$withval], [with_fsmon=no])

AC_ARG_WITH([generic],
	[AS_HELP_STRING([--with-generic], [Enable generic script monitor])],
	[with_generic=$withval], [with_generic=no])

AC_ARG_WITH([loadavg],
        AS_HELP_STRING([--with-loadavg], [Enable CPU load average monitor]),
	[with_loadavg=$withval], [with_loadavg=no])

AC_ARG_WITH([meminfo],
	[AS_HELP_STRING([--with-meminfo], [Enable memory leak monitor])],
	[with_meminfo=$withval], [with_meminfo=no])

AC_ARG_WITH([tempmon],
	[AS_HELP_STRING([--with-tempmon], [Enable temperature monitor])],
	[with_tempmon=$withval], [with_tempmon=no])

AC_ARG_WITH([systemd],
	[AS_HELP_STRING([--with-systemd=DIR], [Directory for systemd service files, default: auto])],,
	[with_systemd=auto])

AS_IF([test "x$enable_compat" = "xyes"], [
	AC_DEFINE(COMPAT_SUPERVISOR,  1, [Enable compat /run/supervisor.status and signals])])

AS_IF([test "x$with_loadavg" != "xno"], [
	with_loadavg=yes
	AC_DEFINE_UNQUOTED(LOADAVG_PLUGIN, $with_loadavg, [Enable CPU load average monitor])])

AS_IF([test "x$with_filenr" != "xno"], [
	with_filenr=yes
	AC_DEFINE_UNQUOTED(FILENR_PLUGIN, $with_filenr, [Enable file descriptor leak monitor])])

AS_IF([test "x$with_fsmon" != "xno"], [
	with_fsmon=yes
	AC_DEFINE_UNQUOTED(FSMON_PLUGIN, $with_fsmon, [Enable file system monitor])])

AS_IF([test "x$with_meminfo" != "xno"], [
	with_meminfo=yes
	AC_DEFINE_UNQUOTED(MEMINFO_PLUGIN, $with_meminfo, [Enable memory leak monitor])])

AS_IF([test "x$with_tempmon" != "xno"], [
	with_tempmon=yes
	AC_DEFINE_UNQUOTED(TEMPMON_PLUGIN, $with_tempmon, [Enable memory leak monitor])])

AS_IF([test "x$with_generic" != "xno"], [
	with_generic=yes
	AC_DEFINE_UNQUOTED(GENERIC_PLUGIN, $with_generic, [Enable generic script monitor])])

AS_IF([test "x$enable_test_mode" != "xno"], enable_test_mode="yes",[
	AC_DEFINE(TESTMODE_DISABLED,  1, [Disable test mode])])

AS_IF([test "x$enable_builtin_tests" != "xno"], enable_builtin_tests="yes",[
	AC_DEFINE(SUPERVISOR_TESTS_DISABLED,  1, [Disable process supervisor tests in watchdogctl])])

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

AM_CONDITIONAL([HAVE_SYSTEMD],  [test "x$with_systemd"  != "xno"])
AM_CONDITIONAL(ENABLE_EXAMPLES, [test "$enable_examples" = yes])
AM_CONDITIONAL(FILENR_PLUGIN,   [test "x$with_filenr"   != "xno"])
AM_CONDITIONAL(FSMON_PLUGIN,    [test "x$with_fsmon"    != "xno"])
AM_CONDITIONAL(GENERIC_PLUGIN,  [test "x$with_generic"  != "xno"])
AM_CONDITIONAL(LOADAVG_PLUGIN,  [test "x$with_loadavg"  != "xno"])
AM_CONDITIONAL(MEMINFO_PLUGIN,  [test "x$with_meminfo"  != "xno"])
AM_CONDITIONAL(TEMPMON_PLUGIN,  [test "x$with_tempmon"  != "xno"])

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
SYSCONFDIR=`eval echo $sysconfdir`
AC_SUBST(SYSCONFDIR)

# Generate all files
AC_OUTPUT

# Expand directories for configuration summary, unexpanded defaults:
# sysconfdir  => ${prefix}/etc
# runstatedir => ${localstatedir}/run
SYSCONFDIR=`eval echo $sysconfdir`
RUNSTATEDIR=`eval echo $runstatedir`
RUNSTATEDIR=`eval echo $RUNSTATEDIR`

cat <<EOF

------------------ Summary ------------------
 $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix..............: $prefix
  Sysconfdir..........: $SYSCONFDIR
  Runstatedir.........: $RUNSTATEDIR
  C Compiler..........: $CC $CFLAGS $CPPFLAGS $LDFLAGS $LIBS

 Optional features:
  compat mode.........: $enable_compat
  client examples.....: $enable_examples
  test mode...........: $enable_test_mode
  built-in tests......: $enable_builtin_tests
  systemd.............: $with_systemd

 System monitors:
  generic script .....: $with_generic
  file descriptor ....: $with_filenr
  file system ........: $with_fsmon
  load average .......: $with_loadavg
  memory leak ........: $with_meminfo
  temperature ........: $with_tempmon

------------- Compiler version --------------
$($CC --version || true)
---------------------------------------------

Check the above options and compile with:
 ${MAKE-make}

EOF
