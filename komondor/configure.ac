dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(komondor, 1.0.0, narate@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([-Wno-portability])
AM_PROG_LIBTOOL

dnl Other libraries

AM_CONDITIONAL([ENABLE_OVIS_LIB], [true])
OPTION_WITH([ovis-lib], [OVIS_LIB])

AM_CONDITIONAL([ENABLE_SOS], [true])
OPTION_WITH([sos], [SOS])

OPTION_WITH_EVENT

dnl OCM
OPTION_DEFAULT_ENABLE([ocm], [ENABLE_OCM])
OPTION_WITH([ocm], [OCM])

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

dnl Check for zap support
AC_CHECK_LIB(zap, zap_new, [],
    AC_MSG_ERROR([zap library not found.  ldms requires libzap. libzap is
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

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 src/test/Makefile
		 src/store/Makefile
		 ])
AC_OUTPUT

