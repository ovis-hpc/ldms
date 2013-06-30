dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(libzap_rdma, 1.0.0, tom@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR(src/zap_rdma.c)
AM_INIT_AUTOMAKE(libzap_rdma, 1.0.0)
AM_CONFIG_HEADER(config.h)
AC_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

AC_C_CONST

dnl Checks for header files.
AC_CHECK_HEADER(infiniband/driver.h, [],
    AC_MSG_ERROR([<infiniband/driver.h> not found.  Is libibverbs installed?]))
AC_HEADER_STDC

dnl Checks for library functions
AC_CHECK_FUNCS(ibv_read_sysfs_file)

AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi)

AC_OUTPUT(Makefile)
