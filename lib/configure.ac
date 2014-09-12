dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-lib], 2.1.0, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign])
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
AC_C_FLEXIBLE_ARRAY_MEMBER

OVIS_PKGLIBDIR


OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])
dnl we need libevent to support zap
if test "$disable_zap" != "yes"; then
  OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
  OPTION_WITH([libevent], [LIBEVENT])
  OPTION_WITH_EVENT

dnl we need libibverbs-devel and librdmacm-devel to support rdma
  if test "$disable_rdma" != "yes"; then
    AC_CHECK_HEADER([infiniband/verbs.h],
    [AC_DEFINE([HAVE_VERBS_H], [1],
      [Define to 1 if you have infiniband/verbs.h.])],
    [AC_MSG_ERROR([Missing header. libibverbs-devel not installed?])])

    AC_CHECK_HEADER([rdma/rdma_cma.h],
    [AC_DEFINE([HAVE_RDMA_CMA_H], [1],
               [Define to 1 if you have rdma/rdma_cma.h.])],
    [AC_MSG_ERROR([Missing header. librdmacm-devel not installed?])])
  fi
else
dnl  libevent only needed for zap.
LIBEVENT_INCDIR_FLAG=""
AC_SUBST(LIBEVENT_INCDIR_FLAG)
ENABLE_LIBEVENT_TRUE='#'
ENABLE_LIBEVENT_FALSE=
fi
OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])

OPTION_WITH_MAGIC([LDMS_MSG_MAX],[32768],[Maximum control message length allowed to ldmsd (min 4095)])

AC_SUBST(ac_configure_args)

AC_OUTPUT(Makefile
	  ovis-lib.spec
	  Doxyfile
	  packaging/ovis-lib-toss.spec
	  src/Makefile src/coll/Makefile
	  src/mmalloc/Makefile
	  src/ovis_ctrl/Makefile
	  src/ovis_util/Makefile
	  src/zap/Makefile
	  src/zap/rdma/Makefile
	  src/zap/sock/Makefile
	  src/zap/test/Makefile)

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
