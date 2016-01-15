# NOTE: autoscan can automatically generate configure.ac
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([baler], 2.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/baler/balerd.c])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/baler-config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

LT_INIT

AC_PROG_CC()
AC_PROG_CXX()
AM_PROG_CC_C_O()

OVIS_PKGLIBDIR

OPTION_DEFAULT_ENABLE([dict], [ENABLE_DICT])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])
dnl Swig stuffs
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AM_PATH_PYTHON(2.7)
	AC_PROG_SWIG
	SWIG_PYTHON
fi

OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT],[/usr])
OPTION_WITH_EVENT

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[coll zap ovis_auth],[ovis-auth.sh],[ovis-lib],[ovis_auth])
AC_SUBST(OVIS_AUTH_LIBS)
dnl we can disable our build rules for outer make dist to work when upper level disables baler.
OPTION_DEFAULT_ENABLE([baler], [ENABLE_BALER], [  requires sos])

if test "$disable_baler" = "yes"; then
	# we're just here for configure so dist is correct.
	enable_sos=no
	enable_ocm=no
fi

OPTION_DEFAULT_ENABLE([sos], [ENABLE_SOS])
OPTION_WITH_OR_BUILD([sos], [../sos],[sos/src sos/include ods/src ods/include])
OPTION_DEFAULT_DISABLE([ocm], [ENABLE_OCM])
OPTION_WITH_OR_BUILD([ocm], [../ocm], [src src/ocm])

if test -z "$ENABLE_OCM_TRUE"; then
	AC_DEFINE_UNQUOTED([ENABLE_OCM],[1],
		[configured with ocm (1) or not (undefined)])
fi

if test -z "$ENABLE_SOS_TRUE"; then
	AC_DEFINE_UNQUOTED([ENABLE_SOS],[1],
		[configured with sos (1) or not (undefined)])
fi

OPTION_DOC

if test "x$WITH_OVIS_LIB" != "xbuild"; then
	AC_MSG_NOTICE([WITH_OVIS_LIB=$WITH_OVIS_LIB])
	dnl Check for zap
	AC_CHECK_LIB(zap, zap_new, [],
		AC_MSG_ERROR([zap not found. Baler requires zap from ovis-lib]),
		$OVIS_LIB_LIBDIR_FLAG $OVIS_LIB_LIB64DIR_FLAG
		)
	dnl Reset LIBS variable.
	LIBS=""
fi

if test "x$WITH_OCM" != "xbuild"; then
	dnl Check for ocm
	if test -z "$ENABLE_OCM_TRUE"
	then
	AC_CHECK_LIB(ocm, ocm_create, [],
		     AC_MSG_ERROR([ocm library not found. Please see OVIS/ocm.]),
		     $OCM_LIBDIR_FLAG $OCM_LIB64DIR_FLAG
		     )
	LIBS=""
	fi
fi

if test "x$WITH_SOS" != "xbuild"; then
	AC_MSG_NOTICE([WITH_OVIS_LIB=$WITH_OVIS_LIB])
	dnl Check for zap
	AC_CHECK_LIB(sos, sos_container_open, [],
		AC_MSG_ERROR([sos not found. Baler requires sos (please see OVIS/sos)]),
		$SOS_LIBDIR_FLAG $SOS_LIB64DIR_FLAG
		)
	dnl Reset LIBS variable.
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

distdir=${PACKAGE_NAME}-${PACKAGE_VERSION}
AC_SUBST(ac_configure_args)
AC_SUBST(prefix)
AC_SUBST(distdir)

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
if test -z "$ENABLE_DOC_TRUE"
then
	AC_CONFIG_FILES([docs/Doxyfile])
fi

AC_CONFIG_FILES([Makefile docs/Makefile src/Makefile
	src/baler/Makefile src/plugins/Makefile
	src/query/Makefile src/test/Makefile
	src/bhttpd/Makefile
	src/bassoc/Makefile
	src/tools/Makefile
	src/plugins/test/Makefile swig/Makefile etc/Makefile
	etc/init.d/Makefile etc/init.d/balerd
	etc/logrotate.d/Makefile
	dict/Makefile
	rpm/rhel7.spec
])

AC_OUTPUT
