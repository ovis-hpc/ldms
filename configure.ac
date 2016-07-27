dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ldms-all, 2.4.8, ovis-help@sandia.gov)
#fixme=--subdir
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/Ovis-top.m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SUBDIRS([lib ldms])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign -Wno-portability ]m4_ifndef([AM_SILENT_RULES],[],[silent-rules])[])
m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes])
AM_PROG_LIBTOOL
AC_LIB_RPATH
SPLIT_PACKAGE_VERSION


dnl Checks for programs
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# if LIBEVENT_BUILD=1 is in the environment, untar and install libevent to our install tree
OPTION_BUILD_LIBEVENT

# LIBEVENT-start
dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])


dnl OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
dnl OPTION_WITH_OR_BUILD([sos],[../sos/src])

dnl if test "$enable_sos" = "yes"; then
dnl AC_CONFIG_SUBDIRS([sos])
dnl fi

# LIBEVENT-ow
OPTION_WITH([libevent], [LIBEVENT])

dnl Check for libevent
# LIBEVENT-owe
OPTION_WITH_EVENT
# LIBEVENT-done

OPTION_DEFAULT_DISABLE([libgenders], [ENABLE_LIBGENDERS])

if test -z "$ENABLE_LIBGENDERS_TRUE"
then
	OPTION_WITH([libgenders], [LIBGENDERS])
	AC_LANG_PUSH([C++])
	BOOST_REQUIRE( ,[AC_MSG_ERROR([Genders enabled but boost not found.])])
	BOOST_STRING_ALGO
	save_LDFLAGS=$LDFLAGS
	LDFLAGS=$LIBGENDERS_LIBDIR_FLAG
	AX_CXX_CHECK_LIB(gendersplusplus,[Gendersplusplus::GendersException], [],
		AC_MSG_ERROR([libgendersplusplus not found. ]))
	LDFLAGS=$save_LDFLAGS
	AC_LANG_POP([C++])
	LIBS=""
fi

OPTION_GITINFO
OPTION_HOSTINFO


dnl Check for glib
PKG_CONFIG=`which pkg-config`
PKG_CHECK_MODULES([GLIB20], [glib-2.0 >= 2.0.0])

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
util/Makefile
util/man/Makefile
util/sample_init_scripts/Makefile
util/sample_init_scripts/hackable/Makefile
util/sample_init_scripts/rhel/Makefile
util/sample_init_scripts/rhel/etc/Makefile
util/sample_init_scripts/rhel/etc/init.d/Makefile
util/sample_init_scripts/rhel/etc/ldms/Makefile
util/sample_init_scripts/rhel/etc/sysconfig/Makefile
util/sample_init_scripts/genders/Makefile
util/sample_init_scripts/genders/etc/sysconfig/Makefile
util/sample_init_scripts/genders/etc/Makefile
util/sample_init_scripts/genders/src/Makefile
util/sample_init_scripts/genders/etc/init.d/Makefile

packaging/ovis-rhel7.spec
packaging/ovis-base.spec])
AC_OUTPUT
