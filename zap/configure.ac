dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(libzap, 1.0.0, tom@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/zap.h])
AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE
AM_PROG_LIBTOOL
AC_PROG_CC
AC_CONFIG_FILES([Makefile])

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

AC_ARG_ENABLE(rdma, [  --enable-rdma     Enable the RDMA transport],
        [       if test x$enableval = xyes ; then
			echo RDMA transport is enabled
			AC_CONFIG_SUBDIRS([rdma])
                fi
        ])

AC_ARG_ENABLE(ugni, [  --enable-ugni     Enable the uGNI transport],
        [       if test x$enableval = xyes ; then
			echo uGNI transport is enabled
			AC_CONFIG_SUBDIRS([ugni])
                fi
        ])

AC_ARG_ENABLE(ugni, [  --enable-ssl     Enable the SSL transport],
        [       if test x$enableval = xyes ; then
			echo SSL transport is enabled
			AC_CONFIG_SUBDIRS([ssl])
                fi
        ])

AC_OUTPUT

