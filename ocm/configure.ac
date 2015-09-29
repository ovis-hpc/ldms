dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([ocm], 1.0.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/ocm/ocm.c])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/ocm-config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CC
AC_CHECK_SIZEOF(long)

dnl check for python interpreter
AM_PATH_PYTHON([2.6])

dnl Needed for per-product flags
AM_PROG_CC_C_O

AC_C_CONST
OVIS_PKGLIBDIR

OPTION_WITH_EVENT

OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[mmalloc coll ovis_util ovis_ctrl],[ovis-auth.sh],[ovis-lib],[ovis_auth])
AC_SUBST(OVIS_AUTH_LIBS)

OPTION_DEFAULT_ENABLE([ovis_rctrl], [ENABLE_OVIS_RCTRL])
if test -z $ENABLE_OVIS_RCTRL_TRUE; then
	HAVE_OVIS_RCTRL=1
else
	HAVE_OVIS_RCTRL=0
fi
AC_SUBST(HAVE_OVIS_RCTRL)

OPTION_DEFAULT_DISABLE([ocmd], [ENABLE_OCMD])
OPTION_DEFAULT_DISABLE([parsers], [ENABLE_PARSERS])

dnl Check for sqlite if ocmd is enabled
if test -z "$ENABLE_OCMD_TRUE"
then
AC_CHECK_LIB(sqlite3, sqlite3_open, [],
	     AC_MSG_ERROR([sqlite3 library not found.])
	     )
LIBS=""
fi

dnl Options for plugins
OPTION_DEFAULT_ENABLE([sqlite3], [ENABLE_SQLITE3])
OPTION_WITH([sqlite3], [SQLITE3], [/usr/local])


dnl Check for pthread
AC_CHECK_LIB(pthread, pthread_create, [],
	AC_MSG_ERROR([pthread library not found.]))
dnl Reset LIBS variable.
LIBS=""

if test "x$WITH_OVIS_LIB" != "xbuild"; then
dnl Check for zap
	AC_CHECK_LIB(zap, zap_new, [],
		AC_MSG_ERROR([zap library not found. ocmd requires zap from ovis-lib]),
		$OVIS_LIB_LIBDIR_FLAG)
	LIBS=""
else
	AC_MSG_NOTICE([Using staged ovis-lib $OVIS_LIB_LIB64DIR_FLAG $OVIS_LIB_LIBDIR_FLAG for mmalloc/ and coll/. ])
fi

AC_CONFIG_FILES([Makefile
		src/Makefile
		src/ocm-vars.sh
		src/ocm/Makefile
		src/ocmd/Makefile
		src/test/Makefile
		src/ovis_rctrl/Makefile
		src/plugins/Makefile
		src/parsers/Makefile
		src/parsers/oparser_sqlite/Makefile
		etc/Makefile
		etc/init.d/Makefile
		etc/init.d/ocmd
		etc/logrotate.d/Makefile
])

AC_CONFIG_SUBDIRS([src/parsers/oparser_sqlite/python])

AC_OUTPUT

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi

