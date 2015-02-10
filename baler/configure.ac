# NOTE: autoscan can automatically generate configure.ac
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([baler], 2.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE
LT_INIT

AC_PROG_CC()
AC_PROG_CXX()
AM_PROG_CC_C_O()

OPTION_DEFAULT_ENABLE([dict], [ENABLE_DICT])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])
dnl Swig stuffs
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AM_PATH_PYTHON(2.6)
	AC_PROG_SWIG
	SWIG_PYTHON
fi

OPTION_WITH_EVENT

AM_CONDITIONAL([ENABLE_SOS], [true])
AM_CONDITIONAL([ENABLE_OVIS_LIB], [true])
OPTION_DEFAULT_ENABLE([ocm], [ENABLE_OCM])
dnl OPTION_DEFAULT_ENABLE([sos], [ENABLE_SOS])
OPTION_WITH([sos], [SOS])
OPTION_WITH([ovis-lib], [OVIS_LIB])
OPTION_WITH([ocm], [OCM])

OPTION_DOC

dnl Check for zap
AC_CHECK_LIB(zap, zap_new, [],
	AC_MSG_ERROR([zap library not found. Baler requires libzap. libzap is
		      one of the libraries in lib-ovis (please see OVIS/lib)]),
	$OVIS_LIB_LIBDIR_FLAG)
dnl Reset LIBS variable.
LIBS=""

dnl Check for ocm
if test -z "$ENABLE_OCM_TRUE"
then
AC_CHECK_LIB(ocm, ocm_create, [],
	     AC_MSG_ERROR([ocm library not found. Please see OVIS/ocm.]),
	     $OCM_LIBDIR_FLAG
	     )
LIBS=""
fi

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# For Doxygen
AC_CHECK_PROGS([DOXYGEN], [doxygen])
if test -z "$DOXYGEN"; then
	AC_MSG_WARN([Doxygen not found])
fi

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([docs/Doxyfile])])

AC_CONFIG_FILES([Makefile docs/Makefile src/Makefile
	scripts/Makefile src/baler/Makefile src/plugins/Makefile
	src/query/Makefile src/test/Makefile
	src/plugins/test/Makefile swig/Makefile etc/Makefile
	etc/init.d/Makefile etc/init.d/balerd
	etc/logrotate.d/Makefile
	dict/Makefile
])

AC_OUTPUT
