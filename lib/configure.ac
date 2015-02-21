dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-lib], 2.4.0, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign])
m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes])
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
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])
dnl OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
dnl # we need libevent to support zap
dnl if test "$disable_zap" != "yes"; then
dnl   OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
dnl   OPTION_WITH([libevent], [LIBEVENT])
dnl   OPTION_WITH_EVENT
dnl
dnl # we need libibverbs-devel and librdmacm-devel to support rdma
dnl   if test "$disable_rdma" != "yes"; then
dnl     AC_CHECK_HEADER([infiniband/verbs.h],
dnl     [AC_DEFINE([HAVE_VERBS_H], [1],
dnl       [Define to 1 if you have infiniband/verbs.h.])],
dnl     [AC_MSG_ERROR([Missing header. libibverbs-devel not installed?])])
dnl
dnl     AC_CHECK_HEADER([rdma/rdma_cma.h],
dnl     [AC_DEFINE([HAVE_RDMA_CMA_H], [1],
dnl                [Define to 1 if you have rdma/rdma_cma.h.])],
dnl     [AC_MSG_ERROR([Missing header. librdmacm-devel not installed?])])
dnl   fi
dnl else
dnl # libevent only needed for zap.
dnl LIBEVENT_INCDIR_FLAG=""
dnl AC_SUBST(LIBEVENT_INCDIR_FLAG)
dnl ENABLE_LIBEVENT_TRUE='#'
dnl ENABLE_LIBEVENT_FALSE=
dnl fi
OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
dnl OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])

OPTION_WITH_MAGIC([LDMS_MSG_MAX],[32768],[Maximum control message length allowed to ldmsd (min 4095)])

AC_SUBST(ac_configure_args)

AC_OUTPUT(Makefile
	  ovis-lib.spec
	  Doxyfile
	  packaging/ovis-lib-toss.spec
	  src/Makefile src/coll/Makefile
	  src/mmalloc/Makefile
	  src/ovis_ctrl/Makefile
	  src/ovis_util/Makefile)
dnl	  src/zap/Makefile
dnl	  src/zap/rdma/Makefile
dnl	  src/zap/sock/Makefile
dnl	  src/zap/test/Makefile)

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
