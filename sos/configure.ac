dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
dnl AC_C_BIGENDIAN is broken in several versions before 2.63
AC_INIT(ovis-sos, 1.0.1, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/sos.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE()
AC_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)

dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O

dnl Swig stuffs
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
AM_PATH_PYTHON(2.6)
AC_PROG_SWIG
SWIG_PYTHON
fi

dnl Check host byte order
AC_C_BIGENDIAN(
	AC_DEFINE(SOS_OBJ_BYTE_ORDER, SOS_OBJ_BE, [machine is big endian]),
	AC_DEFINE(SOS_OBJ_BYTE_ORDER, SOS_OBJ_LE, [machine is little endian]),
	AC_MSG_ERROR(unknown endianness),
	AC_MSG_ERROR(univeral endianness not supported)
)

AC_C_CONST


AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi)

dnl
OVIS_LIB_PREFIX=/usr
OVIS_LIB_INCLUDE=/usr/include
AC_ARG_WITH([ovis-prefix],
	[AS_HELP_STRING([--with-ovis-prefix],
              [Specify prefix of ovis lib and devel installation])],
            [],
            [with_ovis_prefix=check])
if test "x$with_ovis_prefix" != "xno" ;  then
	if test -f "$with_ovis_prefix/include/ovis-test/test.h"; then
		OVIS_LIB_PREFIX=$with_ovis_prefix
		OVIS_LIB_INCLUDE=$with_ovis_prefix/include
		CPPFLAGS="-I$OVIS_LIB_INCLUDE $CPPFLAGS"
	else
		if test -f /opt/ovis/include/ovis-test/test.h; then
			OVIS_LIB_PREFIX=/opt/ovis
			OVIS_LIB_INCLUDE=/opt/ovis/include
			CPPFLAGS="-I$OVIS_LIB_INCLUDE $CPPFLAGS"
		fi
	fi
else
   AC_MSG_ERROR([ovis prefix is not optional.])
fi

AC_CHECK_HEADER([ovis-test/test.h],[],
   [AC_MSG_ERROR([Missing header ovis-test/test.h. Specify --with-ovis-prefix=OVISPREFIX])])

AC_SUBST(OVIS_LIB_PREFIX)
AC_SUBST(OVIS_LIB_INCLUDE)
rpm_configure_args=`echo $ac_configure_args | sed -e "s%--prefix=[^']*%%"`
AC_SUBST(rpm_configure_args)

AM_CONDITIONAL([HAVE_LD_VERSION_SCRIPT], [test "$ac_cv_version_script" = "yes"])
AC_OUTPUT(Makefile src/Makefile swig/Makefile packaging/ovis-sos.spec Doxyfile)
