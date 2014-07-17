dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ldms-all, 2.1.0, ovis-help@sandia.gov)
#fixme=--subdir
AC_CONFIG_MACRO_DIR([m4])
dnl AC_CONFIG_SRCDIR([src/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SUBDIRS([lib ldms])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign -Wno-portability])
AM_PROG_LIBTOOL


dnl Checks for programs
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# LIBEVENT-start
dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])


OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
dnl OPTION_WITH_OR_BUILD([sos],[../sos/src])

if test "$enable_sos" = "yes"; then
AC_CONFIG_SUBDIRS([sos])
fi

# LIBEVENT-ow
OPTION_WITH([libevent], [LIBEVENT])

dnl Check for libevent
# LIBEVENT-owe
OPTION_WITH_EVENT
# LIBEVENT-done

OPTION_GITINFO
OPTION_HOSTINFO


dnl Check for glib
PKG_CONFIG=`which pkg-config`
PKG_CHECK_MODULES([GLIB20], [glib-2.0 >= 2.0.0])

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile packaging/ovis-base.spec])
AC_OUTPUT
