dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-helper-scripts], 4.3.1, ovis-help@sandia.gov)
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

AC_SUBST(ac_configure_args)


AC_CONFIG_FILES([Makefile
		scripts/Makefile
		])

AC_OUTPUT()
