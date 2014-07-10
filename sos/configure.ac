dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
dnl AC_C_BIGENDIAN is broken in several versions before 2.63
AC_INIT(ovis-sos, 2.1.0, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/sos.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE()
AC_PROG_LIBTOOL
AC_LIB_RPATH

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
OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])

dnl Check host byte order
AC_C_BIGENDIAN(
	AC_DEFINE(SOS_OBJ_BYTE_ORDER, SOS_OBJ_BE, [machine is big endian]),
	AC_DEFINE(SOS_OBJ_BYTE_ORDER, SOS_OBJ_LE, [machine is little endian]),
	AC_MSG_ERROR(unknown endianness),
	AC_MSG_ERROR(univeral endianness not supported)
)

AC_C_CONST
AC_C_FLEXIBLE_ARRAY_MEMBER


AC_CACHE_CHECK(whether ld accepts --version-script, ac_cv_version_script,
    if test -n "`$LD --help < /dev/null 2>/dev/null | grep version-script`"; then
        ac_cv_version_script=yes
    else
        ac_cv_version_script=no
    fi)

dnl
AC_ARG_WITH([ovis-prefix],[AS_HELP_STRING([--with-ovis-prefix],
              [deprecated. Use --with-ovis-include instead.])],
            [AC_MSG_ERROR([Deprecated --with-ovis-prefix. Use --with-ovis-include instead or omit flag to search in source tree. ])],
             [])

dnl tell us where to find the src tree or identical previously installed header set
dnl or we will try to find by relative path assuming $srctree/[lib,sos,ldms]
dnl structure used in dec 2013
AC_ARG_WITH([ovis-include],
	[AS_HELP_STRING([--with-ovis-include],
              [Specify location (installed or src) containing ovis-test/test.h])],
            [], [with_ovis_include=check])
if test "x$with_ovis_include" != "xno" ;  then
	if test -f "$with_ovis_include/ovis-test/test.h"; then
		OVIS_LIB_INCLUDE=$with_ovis_include
		CPPFLAGS="-I$OVIS_LIB_INCLUDE $CPPFLAGS"
	else
		if test -d $srcdir/../lib/src; then
			libsrcdir=`(cd $srcdir/../lib/src && pwd)`
		fi
		if test -f $libsrcdir/ovis-test/test.h; then
			OVIS_LIB_INCLUDE=$libsrcdir
			CPPFLAGS="-I$OVIS_LIB_INCLUDE $CPPFLAGS"
		else 
			AC_MSG_ERROR([could not find given ovis lib headers (in $with_ovis_include) or $srcdir/../lib/src. what?])
		fi
	fi
else
   AC_MSG_ERROR([ovis includes are required.])
fi
AC_MSG_NOTICE([Found OVIS_LIB_INCLUDE=$OVIS_LIB_INCLUDE])

AC_CHECK_HEADER([ovis-test/test.h],[],
   [AC_MSG_ERROR([Missing header ovis-test/test.h. Specify --with-ovis-include=OVISPREFIX_or_LIB_SRC_DIR])])

AC_SUBST(OVIS_LIB_INCLUDE)
rpm_configure_args=`echo $ac_configure_args | sed -e "s%--prefix=[^']*%%"`
AC_SUBST(rpm_configure_args)

AM_CONDITIONAL([HAVE_LD_VERSION_SCRIPT], [test "$ac_cv_version_script" = "yes"])
AC_OUTPUT(Makefile src/Makefile swig/Makefile packaging/ovis-sos.spec Doxyfile)

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
