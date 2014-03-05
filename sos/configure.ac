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

OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])

dnl Swig stuffs
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
AM_PATH_PYTHON(2.6)
AC_PROG_SWIG
SWIG_PYTHON
pkgpythondir="${pythondir}/libsos"
pkgpyexecdir="${pkgpythondir}"
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

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH([ovis-lib], [OVIS_LIB])

AC_CHECK_LIB(coll, idx_create, [],
	     AC_MSG_ERROR([libcoll not found. Please install libcoll from
			   OVIS/lib.]),
	     $OVIS_LIB_LIBDIR_FLAG)
dnl Reset LIBS variable.
LIBS=""

rpm_configure_args=`echo $ac_configure_args | sed -e "s%--prefix=[^']*%%"`
AC_SUBST(rpm_configure_args)

AM_CONDITIONAL([HAVE_LD_VERSION_SCRIPT], [test "$ac_cv_version_script" = "yes"])
AC_OUTPUT(Makefile src/Makefile swig/Makefile packaging/ovis-sos.spec Doxyfile)
