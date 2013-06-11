dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(libcoll, 1.0.0, tom@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/rbt.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(libcoll, 1.0.0)
AM_INIT_AUTOMAKE(libmmalloc, 1.0.0)
AC_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

AC_C_CONST

AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi)

AM_CONDITIONAL([HAVE_LD_VERSION_SCRIPT], [test "$ac_cv_version_script" = "yes"])
dnl AC_CONFIG_FILES([Makefile libcoll.spec])
AC_OUTPUT(Makefile src/Makefile)
