dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-lib], 4.1.1, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/ovis-lib-config.h,,,[
#ifndef OVIS_FLEX
/* If OVIS_LIB_FLEXIBLE_ARRAY_MEMBER is undefined or 1, we use 0
   for flexible array size on non ISO C compilers,
   else we use empty [] per c99.
   If a compiler is found not supporting both flex arrays and
   0 size array declarations, it will need an OVIS_FLEX value of 1
   and all sizeof calls on the affected structures will need
   offsetof instead, as outlined in ac_c_flexible_array_member source.
*/
#if (OVIS_LIB_FLEXIBLE_ARRAY_MEMBER + 0)
#define OVIS_FLEX 0
#else
#define OVIS_FLEX
#endif
/* A union of flexible array types is not allowed by gcc 4.
   If this changes in a future compiler, this define may need changing.
   The ldms metric value union needs this.
*/
#define OVIS_FLEX_UNION 0
#endif
])
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

AC_SEARCH_LIBS([clock_gettime],[rt posix4],[LDFLAGS_GETTIME=$LIBS])
AC_SUBST([LDFLAGS_GETTIME])
LIBS=""

OPTION_DOC
OPTION_GITINFO

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([ovis_event], [ENABLE_OVIS_EVENT])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([ovis_auth], [ENABLE_OVIS_AUTH])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_WITH_OR_BUILD([sos],[../sos],[sos/src sos/include ods/src ods/include])
have_zap=0

if test "$disable_zap" != "yes"; then
  have_zap=1
dnl we need libibverbs-devel and librdmacm-devel to support rdma
  if test "$enable_rdma" = "yes"; then
    AC_CHECK_HEADER([infiniband/verbs.h],
    [AC_DEFINE([HAVE_VERBS_H], [1],
      [Define to 1 if you have infiniband/verbs.h.])],
    [AC_MSG_ERROR([Missing header. libibverbs-devel not installed?])])

    AC_CHECK_HEADER([rdma/rdma_cma.h],
    [AC_DEFINE([HAVE_RDMA_CMA_H], [1],
               [Define to 1 if you have rdma/rdma_cma.h.])],
    [AC_MSG_ERROR([Missing header. librdmacm-devel not installed?])])
  fi
  if test "$enable_ugni" = "yes"; then
    m4_ifndef([PKG_CHECK_MODULES],
              [m4_fatal([pkg.m4 not found. Please install pkg-config (Ubuntu) or pkgconfig (RHEL) package])])
    PKG_CHECK_MODULES([UGNI], [cray-ugni],
		      [AC_DEFINE([HAVE_CRAY_UGNI],[1],[if cray-ugni module present.])],
		      AC_MSG_ERROR([pkg-config cray-ugni failed])
		      )
    PKG_CHECK_MODULES([RCA], [cray-rca],
		      [AC_DEFINE([HAVE_CRAY_RCA],[1],[if cray-rca module present.])],
		      AC_MSG_ERROR([pkg-config cray-rca failed])
		      )
  fi
fi
OPTION_WITH([libibverbs], [LIBIBVERBS])
OPTION_WITH([librdmacm], [LIBRDMACM])
dnl next bits need to be outside 'if' or cray fails.
AM_CONDITIONAL([ENABLE_libibverbs], [test "$HAVE_libibverbs" = "yes"])
AM_CONDITIONAL([ENABLE_librdmacm], [test "$HAVE_librdmacm" = "yes"])

AC_DEFINE_UNQUOTED([HAVE_ZAP],["$have_zap"],[configured with zap transport (1) or not (0)])

if test -z "$ENABLE_SOS_TRUE"; then
	AM_PATH_PYTHON(,,[:])
	which cython >/dev/null 2>&1 || AC_MSG_ERROR("cython not found")
	AX_PYTHON_DEVEL()
	if test "x$WITH_SOS" != "xbuild"; then
		OCFLAGS=$CFLAGS
		CFLAGS=-I$SOS_INCDIR
		AC_CHECK_HEADERS(sos/sos.h,
			[],
			AC_MSG_FAILURE([sos/sos.h not found])
		)
		AC_CHECK_LIB(sos, sos_container_open,
			[],
			AC_MSG_FAILURE(libsos not found: required by --enable-sos),
			[ $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG ]
		)
		CFLAGS=$OCFLAGS
		LIBS=""
	else
		AC_MSG_NOTICE([Using staged ovis-sos $OPVIS_SOS_LIB64DIR_FLAG $OVIS_SOS_LIBDIR_FLAG])
	fi
fi

with_ssl=0
have_auth=0
if test "$enable_ovis_auth" = "yes"; then
	have_auth=1
	dnl Check for OpenSSL
	AX_CHECK_OPENSSL([with_ssl=1],[with_ssl=0])
	AUTH_LIB="-lovis_auth"
else
	AUTH_LIB=""
fi
AC_SUBST([AUTH_LIB])
AC_SUBST([HAVE_AUTH],[$have_auth])
AC_DEFINE_UNQUOTED([HAVE_AUTH],[$have_auth],[configured with authentication (1) or not (0)])
AC_DEFINE_UNQUOTED([HAVE_SSL],[$with_ssl],[configured with ssl authentication support (1) or not (0)])

OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])
OPTION_DEFAULT_DISABLE([ovis_event_test], [ENABLE_OVIS_EVENT_TEST])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])


OVIS_PKGLIBDIR

dnl Swig stuff
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AM_PATH_PYTHON([2.6], , [:])
	AC_PROG_SWIG
	SWIG_PYTHON
	pkgpythondir="${pythondir}/ovis_lib"
	pkgpyexecdir="${pkgpythondir}"
fi
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

# define substitutions for configvars and other sed-generated files.
# note carefully the escapes.
OVIS_DO_SUBST([LIB_SUBST_RULE], ["sed \
-e 's,[[@]]mandir[[@]],\$(mandir),g' \
-e 's,[[@]]localedir[[@]],\$(localedir),g' \
-e 's,[[@]]libdir[[@]],\$(libdir),g' \
-e 's,[[@]]psdir[[@]],\$(psdir),g' \
-e 's,[[@]]pdfdir[[@]],\$(pdfdir),g' \
-e 's,[[@]]dvidir[[@]],\$(dvidir),g' \
-e 's,[[@]]htmldir[[@]],\$(htmldir),g' \
-e 's,[[@]]infodir[[@]],\$(infodir),g' \
-e 's,[[@]]docdir[[@]],\$(docdir),g' \
-e 's,[[@]]includedir[[@]],\$(includedir),g' \
-e 's,[[@]]localstatedir[[@]],\$(localstatedir),g' \
-e 's,[[@]]sharedstatedir[[@]],\$(sharedstatedir),g' \
-e 's,[[@]]sysconfdir[[@]],\$(sysconfdir),g' \
-e 's,[[@]]datadir[[@]],\$(datadir),g' \
-e 's,[[@]]datarootdir[[@]],\$(datarootdir),g' \
-e 's,[[@]]libexecdir[[@]],\$(libexecdir),g' \
-e 's,[[@]]sbindir[[@]],\$(sbindir),g' \
-e 's,[[@]]bindir[[@]],\$(bindir),g' \
-e 's,[[@]]prefix[[@]],\$(prefix),g' \
-e 's,[[@]]exec_prefix[[@]],\$(exec_prefix),g' \
-e 's,[[@]]ZAP_LIBPATH[[@]],\$(ZAP_LIBPATH),g' \
-e 's,[[@]]pkglibdir[[@]],\$(pkglibdir),g' \
-e 's,[[@]]pythondir[[@]],\$(pythondir),g' \
"
])
AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([ovis-lib.spec
		Doxyfile
		Makefile
		src/Makefile
		src/third/Makefile
		src/third/json_parser/Makefile
		src/coll/Makefile
		src/mmalloc/Makefile
		src/ovis_event/Makefile
		src/ovis_ctrl/Makefile
		src/ovis_util/Makefile
		src/ovis_auth/Makefile
		src/ovis_auth/ovis-auth.sh
		src/zap/Makefile
		src/zap/rdma/Makefile
		src/zap/sock/Makefile
		src/zap/ugni/Makefile
		src/zap/test/Makefile
		bin/Makefile
		bin/src/Makefile
		python/Makefile
		swig/Makefile
		swig/zap/Makefile
		swig/ovis_auth/Makefile
		etc/Makefile
		etc/ovis/Makefile
		etc/ld.so.conf.d/Makefile
])

AC_OUTPUT

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi

