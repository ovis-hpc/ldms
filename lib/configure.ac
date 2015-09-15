dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-lib], 1.0.1, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign])
AM_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

AC_C_CONST

OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([ovis_auth], [ENABLE_OVIS_AUTH])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
dnl we need libevent to support zap
if test "$disable_zap" != "yes"; then
  OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
  OPTION_WITH_EVENT

dnl we need libibverbs-devel and librdmacm-devel to support rdma
  if test "$enable_rdma" = "yes"; then
    AC_CHECK_HEADER([infiniband/verbs.h],
    [AC_DEFINE([HAVE_VERBS_H], [1],
      [Define to 1 if you have infiniband/verbs.h.])],
    [AC_MSG_ERROR([Missing header. libibverbs-devel not installed?])])

    AC_CHECK_HEADER([rdma/rdma_cma.h],
    [AC_DEFINE([HAVE_RDMA_CMA_H], [1],
               [Define to 1 if you have rdma/rdma_cma.h.])],
    [AC_MSG_ERROR([Missing header. librdmacm-devel not installed?])])
  fi
  if test "$enable_ugni" = "yes"; then
    m4_ifndef([PKG_CHECK_MODULES],
              [m4_fatal([pkg.m4 not found. Please install pkg-confog (Ubuntu) or pkgconfig (RHEL) package])])
    PKG_CHECK_MODULES([UGNI], [cray-ugni],
		      [],
		      AC_MSG_ERROR([pkg-config cray-ugni failed])
		      )
    PKG_CHECK_MODULES([RCA], [cray-rca],
		      [],
		      AC_MSG_ERROR([pkg-config cray-rca failed])
		      )
  fi
fi

if test -z "$ENABLE_OVIS_AUTH"
then
dnl Check for OpenSSL
AX_CHECK_OPENSSL
fi

OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

dnl Swig stuff
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AM_PATH_PYTHON(2.6)
	AC_PROG_SWIG
	SWIG_PYTHON
	pkgpythondir="${pythondir}/ovis_lib"
	pkgpyexecdir="${pkgpythondir}"
fi

AC_SUBST(ac_configure_args)
AC_SUBST(prefix)

AC_CONFIG_FILES([ovis-lib.spec
		Doxyfile
		packaging/ovis-lib-toss.spec
		packaging/ovis-lib-rhel7.spec
		Makefile
		src/Makefile src/coll/Makefile
		src/mmalloc/Makefile
		src/ovis_ctrl/Makefile
		src/ovis_util/Makefile
		src/ovis_auth/Makefile
		src/zap/Makefile
		src/zap/rdma/Makefile
		src/zap/sock/Makefile
		src/zap/ugni/Makefile
		src/zap/test/Makefile
		swig/Makefile
		swig/zap/Makefile
		swig/ovis_auth/Makefile
		etc/Makefile
		etc/ovis/Makefile
		etc/ovis/set-ovis-variables.sh
		etc/ovis/ovis-logrotate
		etc/ld.so.conf.d/Makefile
		etc/ld.so.conf.d/ovis-ld-so.conf
])

AC_OUTPUT
