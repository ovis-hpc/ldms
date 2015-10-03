dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis-all, 3.1.0, ovis-help@sandia.gov)
#fixme=--subdir
AC_CONFIG_MACRO_DIR([m4])
dnl AC_CONFIG_SRCDIR([src/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SUBDIRS([lib sos ldms])
AM_INIT_AUTOMAKE([foreign -Wno-portability])
AM_PROG_LIBTOOL


dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT])
dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Check for libevent
OPTION_WITH_EVENT

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
