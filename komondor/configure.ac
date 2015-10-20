dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(komondor, 1.0.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/komondor.c])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
dnl AM _INIT_AUTOMAKE([-Wno-portability])
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/komondor-config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

AM_PATH_PYTHON(2.6)

dnl Other libraries

OVIS_PKGLIBDIR

OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

AM_CONDITIONAL([ENABLE_OVIS_LIB], [true])
OPTION_WITH_OR_BUILD([ovis-lib], [../lib/src], [coll zap ovis_util])

AM_CONDITIONAL([ENABLE_SOS], [true])
OPTION_WITH_OR_BUILD([sos], [../sos/src], [])

OPTION_WITH_EVENT

dnl OCM
OPTION_DEFAULT_ENABLE([ocm], [ENABLE_OCM])
OPTION_WITH_OR_BUILD([ocm], [../ocm/src], [ocm])

dnl Checks for programs
AC_PROG_CC
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Check for pthread support
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
dnl Reset LIBS variable.
LIBS=""	

if test "x$WITH_OVIS_LIB" != "xbuild"; then
dnl Check for zap support
AC_CHECK_LIB(zap, zap_new, [],
	AC_MSG_ERROR([zap not found.  komondor requires zap from ovis-lib.]),
    $OVIS_LIB_LIBDIR_FLAG)
dnl Reset LIBS variable.
LIBS=""
fi
if test "x$WITH_OCM" != "xbuild"; then
	dnl Check for ocm
	if test -z "$ENABLE_OCM_TRUE"; then
		AC_CHECK_LIB(ocm, ocm_create, [],
			AC_MSG_ERROR([ocm library not found..]),
			$OCM_LIBDIR_FLAG
		)
	LIBS=""
	fi
fi

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 src/test/Makefile
		 src/store/Makefile
		 python/Makefile
		 python/komondor/Makefile
		 etc/Makefile
		 etc/init.d/Makefile
		 etc/init.d/komondor
		 etc/logrotate.d/Makefile
		 ])
AC_OUTPUT
