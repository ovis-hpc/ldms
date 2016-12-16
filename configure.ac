dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis, 3.3.3, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/Ovis-top.m4])
AC_CONFIG_AUX_DIR(config)
AX_PREFIX_CONFIG_H_G(ovis-all-config.h)
AC_CONFIG_SUBDIRS([lib])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_CONFIG_HEADER(config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

AC_C_CONST

OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])

# note: ocm, me, and komondor are not distributed products.
# DIST automake rules must not include them and
# enable-<one of them> is not supported on packaged distributions.

OPTION_DEFAULT_DISABLE([gpcdlocal], [ENABLE_GPCDLOCAL], [  cray only])
if test "$enable_gpcdlocal" = "yes"; then
	AC_CONFIG_SUBDIRS([gpcd-support])
	if ! test -d $srcdir/gpcd-support/gpcd; then
		AC_MSG_ERROR([You need the gpcd-support submodule to enable it.])
	fi
fi

OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS], [  requires lib])
if test "$enable_sos" = "yes"; then
	AC_CONFIG_SUBDIRS([sos])
	if ! test -d $srcdir/sos/sos; then
		AC_MSG_ERROR([You need the sos submodule to enable it.])
	fi
fi

OPTION_DEFAULT_DISABLE([ocm], [ENABLE_OCM], [  requires lib])
if test "$enable_ocm" = "yes"; then
	AC_CONFIG_SUBDIRS([ocm])
fi

# ldms is not optional for now.
OPTION_DEFAULT_ENABLE([ldms], [ENABLE_LDMS],[  requires lib])
AC_CONFIG_SUBDIRS([ldms])

OPTION_DEFAULT_DISABLE([baler], [ENABLE_BALER], [  requires sos])

AC_CONFIG_SUBDIRS([baler])

OPTION_DEFAULT_DISABLE([me], [ENABLE_ME], [  requires lib])
if test "$enable_me" = "yes"; then
	AC_CONFIG_SUBDIRS([me])
fi
OPTION_DEFAULT_DISABLE([komondor], [ENABLE_KOMONDOR], [  requires lib])
if test "$enable_komondor" = "yes"; then
	AC_CONFIG_SUBDIRS([komondor])
fi

dnl Check for libevent
OPTION_WITH([libevent], [LIBEVENT], [/usr])
dnl extra setup
OPTION_WITH_EVENT

SUBST_MAYBE([gpcdlocal])
SUBST_MAYBE([sos])
SUBST_MAYBE([ocm])
SUBST_MAYBE([ldms])
SUBST_MAYBE([baler])
SUBST_MAYBE([me])
SUBST_MAYBE([komondor])

OPTION_DEFAULT_DISABLE([libgenders], [ENABLE_LIBGENDERS],[  requires C++,boost])

if test -z "$ENABLE_LIBGENDERS_TRUE"
then
	OPTION_WITH([libgenders], [LIBGENDERS],[/usr])
	AC_LANG_PUSH([C++])
	BOOST_REQUIRE( , AC_MSG_ERROR([Genders enabled but boost not found.]))
	BOOST_STRING_ALGO
	save_LDFLAGS=$LDFLAGS
	LDFLAGS=$LIBGENDERS_LIBDIR_FLAG
	AX_CXX_CHECK_LIB(gendersplusplus,[Gendersplusplus::GendersException], [],
		AC_MSG_ERROR([libgendersplusplus not found. ]))
	LDFLAGS=$save_LDFLAGS
	AC_LANG_POP([C++])
	LIBS=""
fi

OVIS_PKGLIBDIR
OPTION_GITINFO
OPTION_HOSTINFO


AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
util/Makefile
util/sample_init_scripts/Makefile
util/sample_init_scripts/genders/Makefile
util/sample_init_scripts/genders/etc/sysconfig/Makefile
util/sample_init_scripts/genders/etc/Makefile
util/sample_init_scripts/genders/src/Makefile
util/sample_init_scripts/genders/etc/init.d/Makefile
packaging/ovis-trinity.spec
packaging/ovis-rhine.spec
packaging/ovis-mon7.spec
packaging/ovis-mon6.spec
packaging/ovis-base.spec])
AC_OUTPUT
