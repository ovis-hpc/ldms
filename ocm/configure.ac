dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([ocm], 1.0.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AM_INIT_AUTOMAKE

dnl Checks for programs
AC_PROG_CC
AC_CHECK_SIZEOF(long)

AM_PROG_LIBTOOL

dnl Needed for per-product flags
AM_PROG_CC_C_O

AC_C_CONST

OPTION_WITH_EVENT

OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH([ovis-lib], [OVIS_LIB])

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
OPTION_WITH([sqlite3], [SQLITE3])


dnl Check for pthread
AC_CHECK_LIB(pthread, pthread_create, [],
	AC_MSG_ERROR([pthread library not found.]))
dnl Reset LIBS variable.
LIBS=""

dnl Check for zap
AC_CHECK_LIB(zap, zap_new, [],
	AC_MSG_ERROR([zap library not found. ocmd requires libzap. libzap is
		      one of the libraries in lib-ovis (please see OVIS/lib)]),
	$OVIS_LIB_LIBDIR_FLAG)
dnl Reset LIBS variable.
LIBS=""

AC_OUTPUT(Makefile
	  src/Makefile
	  src/test/Makefile
	  src/plugins/Makefile
	  src/parsers/Makefile
	  src/parsers/oparser_sqlite/Makefile
	  etc/init.d/ocmd
	  etc/Makefile
	)
