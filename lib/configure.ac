dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-lib], 3.0.0, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/ovis-lib-config.h)
AM_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

AC_C_CONST

AC_SEARCH_LIBS([clock_gettime],[rt posix4],[LDFLAGS_GETTIME=$LIBS])
AC_SUBST([LDFLAGS_GETTIME])
LIBS=""

OPTION_DOC

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([ovis_auth], [ENABLE_OVIS_AUTH])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
dnl we need libevent to support zap
have_zap=0

OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT],[/usr])

OPTION_WITH_EVENT
if test "$disable_zap" != "yes"; then
  have_zap=1


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
              [m4_fatal([pkg.m4 not found. Please install pkg-config (Ubuntu) or pkgconfig (RHEL) package])])
    PKG_CHECK_MODULES([UGNI], [cray-ugni],
		      [AC_DEFINE([HAVE_CRAY_UGNI],[1],[if cray-ugni module present.])],
		      AC_MSG_ERROR([pkg-config cray-ugni failed])
		      )
    PKG_CHECK_MODULES([RCA], [cray-rca],
		      [AC_DEFINE([HAVE_CRAY_RCA],[1],[if cray-rca module present.])],
		      AC_MSG_ERROR([pkg-config cray-rca failed])
		      )
  fi
fi
OPTION_WITH([libibverbs], [LIBIBVERBS])
OPTION_WITH([librdmacm], [LIBRDMACM])
dnl next bits need to be outside 'if' or cray fails.
AM_CONDITIONAL([ENABLE_libibverbs], [test "$HAVE_libibverbs" = "yes"])
AM_CONDITIONAL([ENABLE_librdmacm], [test "$HAVE_librdmacm" = "yes"])

AC_DEFINE_UNQUOTED([HAVE_ZAP],["$have_zap"],[configured with zap transport (1) or not (0)])

with_ssl=0
have_auth=0
if test "$enable_ovis_auth" = "yes"; then
	have_auth=1
	dnl Check for OpenSSL
	AX_CHECK_OPENSSL([with_ssl=1],[with_ssl=0])
	AUTH_LIB="-lovis_auth"
else
	AUTH_LIB=""
fi
AC_SUBST([AUTH_LIB])
AC_SUBST([HAVE_AUTH],[$have_auth])
AC_DEFINE_UNQUOTED([HAVE_AUTH],[$have_auth],[configured with authentication (1) or not (0)])
AC_DEFINE_UNQUOTED([HAVE_SSL],[$with_ssl],[configured with ssl authentication support (1) or not (0)])

OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])


OVIS_PKGLIBDIR

dnl Swig stuff
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AM_PATH_PYTHON([2.6], , [:])
	AC_PROG_SWIG
	SWIG_PYTHON
	pkgpythondir="${pythondir}/ovis_lib"
	pkgpyexecdir="${pkgpythondir}"
fi
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([ovis-lib.spec
		Doxyfile
		packaging/ovis-lib-toss.spec
		packaging/ovis-lib-rhel7.spec
		Makefile
		src/ovis-lib-configvars.sh
		src/Makefile src/coll/Makefile
		src/mmalloc/Makefile
		src/ovis_ctrl/Makefile
		src/ovis_util/Makefile
		src/ovis_auth/Makefile
		src/ovis_auth/ovis-auth.sh
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

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi

