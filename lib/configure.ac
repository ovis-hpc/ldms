dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([ovis-lib], 1.0.0, tom@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE
AM_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

AC_C_CONST

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])

AC_SUBST(ac_configure_args)

AC_OUTPUT(Makefile
	  ovis-lib.spec
	  src/Makefile src/coll/Makefile
	  src/mmalloc/Makefile
	  src/ovis_ctrl/Makefile
	  src/ovis_util/Makefile
	  src/zap/Makefile
	  src/zap/rdma/Makefile
	  src/zap/test/Makefile)
