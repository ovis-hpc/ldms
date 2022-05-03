dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-ldms],
        m4_esyscmd([config/git-version-gen --prefix 'OVIS-' .tarball-version]),
        [ovis-help@sandia.gov])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/Ovis-top.m4])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AX_PREFIX_CONFIG_H_G(ovis-ldms-config.h,,,[
#ifndef OVIS_FLEX
/* If OVIS_LDMS_FLEXIBLE_ARRAY_MEMBER is undefined or 1, we use 0
   for flexible array size on non ISO C compilers,
   else we use empty [] per c99.
   If a compiler is found not supporting both flex arrays and
   0 size array declarations, it will need an OVIS_FLEX value of 1
   and all sizeof calls on the affected structures will need
   offsetof instead, as outlined in ac_c_flexible_array_member source.
*/
#if (OVIS_LDMS_FLEXIBLE_ARRAY_MEMBER + 0)
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

dnl if LEX is not available, the configure still won't fail
AC_PROG_LEX
AC_PROG_LN_S

AC_ARG_VAR([BISON], [bison command])
AC_CHECK_PROG([BISON], [bison -y], [bison -y], [no])
AS_IF([test "x$BISON" = "xno"], [AC_MSG_ERROR([bison not found])])

dnl Checks for programs
AC_PROG_CXX
dnl Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long double)
dnl Needed for per-product flags
AC_PROG_CC_C_O
AM_PROG_CC_C_O
AC_C_CONST
AC_C_FLEXIBLE_ARRAY_MEMBER

OVIS_PKGLIBDIR

dnl change sharedstatedir default
test "$sharedstatedir" = '${prefix}/com' && sharedstatedir='${prefix}/var/lib'

#
# link and include flags for build to ensure self-consistency
#

# ideally, this should yield from lib/src ldms/src
# "-I$(top_builddir)/lib/src -I$(top_builddir)/ldms/src -I$(top_srcdir)/lib/src -I$(top_srcdir)/ldms/src"
# when all the includes are fixed.
dnl contrib should never be in this list
OPTION_INCLUDE_FLAGS([OVIS],[ \
	. \
	lib/src \
	lib/src/coll \
	lib/src/third \
	lib/src/ovis_util \
	lib/src/ovis_auth \
	lib/src/ovis_ctrl \
	lib/src/ovis_ev \
	lib/src/ovis_event \
	lib/src/ovis_json \
	lib/src/zap \
	lib/src/mmalloc \
	ldms/src \
	ldms/src/core \
	ldms/src/auth \
	ldms/src/ldmsd \
	ldms/src/sampler \
	ldms/src/store \
])

dnl contrib subdirs should never be in this list
OPTION_LIB_FLAGS([OVIS],[ \
	lib/src/coll \
	lib/src/third \
	lib/src/ovis_util \
	lib/src/ovis_auth \
	lib/src/ovis_ctrl \
	lib/src/ovis_ev \
	lib/src/ovis_event \
	lib/src/ovis_json \
	lib/src/zap \
	lib/src/mmalloc \
	ldms/src/core \
	ldms/src/auth \
	ldms/src/ldmsd \
	ldms/src/sampler \
	ldms/src/sampler/lustre \
	ldms/src/store \
])

## stuff from lib

AC_SEARCH_LIBS([clock_gettime],[rt posix4],[LDFLAGS_GETTIME=$LIBS])
AC_SUBST([LDFLAGS_GETTIME])
LIBS=""

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([ovis_ev], [ENABLE_OVIS_EV])
OPTION_DEFAULT_ENABLE([ovis_event], [ENABLE_OVIS_EVENT])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([ovis_auth], [ENABLE_OVIS_AUTH])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([nola], [ENABLE_NOLA])
OPTION_DEFAULT_DISABLE([fabric], [ENABLE_FABRIC])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
dnl dnl OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
dnl dnl OPTION_WITH_OR_BUILD([sos],[../sos],[sos/src sos/include ods/src ods/include])

# PKG_PROG_PKG_CONFIG before all PKG_CHECK_MODULES may or not be called, per pkg.m4.
m4_ifndef([PKG_CHECK_MODULES],
      [m4_fatal([pkg.m4 not found. Please install pkg-config (Ubuntu) or pkgconfig (RHEL) package])])
PKG_PROG_PKG_CONFIG

dnl RDC defaults to no instead of search because amd currently recommends
dnl multiple non-default path installations (/opt).
AC_ARG_ENABLE([rdc],
	[AS_HELP_STRING([--enable-rdc], [attempt components that depend upon AMD rdc tooling @<:@default=no@:>@ for GPUs. Set RDC_CFLAGS=-I$rdcinclude and RDC_LIBS=-L$rdclibdir])],
	[],
	[enable_rdc="no"])
if test "$enable_rdc" != "no"; then
    PKG_CHECK_MODULES([RDC], [rdc],
		      [AC_DEFINE([HAVE_RDC],[1],[if AMD rdc module present.])],
		      AC_MSG_ERROR([pkg-config rdc failed])
		      )
    AC_MSG_RESULT([rdc include: $RDC_CFLAGS])
    AC_MSG_RESULT([rdc lib: $RDC_LIBS])
    HAVE_RDC=yes
fi
AM_CONDITIONAL([ENABLE_RDC], [test "x$HAVE_RDC" = xyes])

have_zap=0

if test "$disable_zap" != "yes"; then
  have_zap=1
dnl we need libibverbs-devel and librdmacm-devel to support rdma
  if test "$enable_rdma" = "yes"; then
    AC_CHECK_HEADER([infiniband/verbs.h],
    [ AC_DEFINE([HAVE_VERBS_H] , [1], [Define to 1 if you have infiniband/verbs.h.])
	IBVERBS_ENUM_CHECK([ibvwct_enums])
        if test "$ibvwct_enums" = "1"; then
		AC_DEFINE([HAVE_VERBS_IBVWCT_ENUMS], [1],
		[Defined to 1 if you have IBV_WC_T* enum values in verbs.h.])
	fi
	],
    [AC_MSG_ERROR([Missing header. libibverbs-devel not installed?])])

    AC_CHECK_HEADER([rdma/rdma_cma.h],
    [AC_DEFINE([HAVE_RDMA_CMA_H], [1],
               [Define to 1 if you have rdma/rdma_cma.h.])],
    [AC_MSG_ERROR([Missing header. librdmacm-devel not installed?])])
  fi
  if test "$enable_ugni" = "yes"; then
    PKG_CHECK_MODULES([UGNI], [cray-ugni],
		      [AC_DEFINE([HAVE_CRAY_UGNI],[1],[if cray-ugni module present.])],
		      AC_MSG_ERROR([pkg-config cray-ugni failed])
		      )
    PKG_CHECK_MODULES([RCA], [cray-rca],
		      [AC_DEFINE([HAVE_CRAY_RCA],[1],[if cray-rca module present.])],
		      AC_MSG_ERROR([pkg-config cray-rca failed])
		      )
  fi
  if test "$enable_fabric" = "yes"; then
    AC_ARG_WITH([libfabric],
      AC_HELP_STRING([--with-libfabric], [Specify libfabric location]),
        [AS_IF([test -d "$withval/lib"], [LIB_FABRIC="$withval"],
          AC_MSG_ERROR([libfabric not found at $withval]))],
          AC_MSG_ERROR([must specify --with-libfabric]))
    LDFLAGS="$LDFLAGS -L${LIB_FABRIC}/lib"
    AC_SUBST([LIBFABRIC_INCDIR], [${LIB_FABRIC}/include])
    saveCPPFLAGS=$CPPFLAGS
    CPPFLAGS="$CPPFLAGS -I${LIB_FABRIC}/include"
    AC_CHECK_LIB([fabric], fi_getinfo, [],
      AC_MSG_ERROR([fi_getinfo() not found. zap requires fabric.]))
    AC_CHECK_HEADER([rdma/fabric.h], [],
      [AC_MSG_ERROR([<rdma/fabric.h> not found. zap requires fabric.])])
    AC_MSG_RESULT([Using fabric at $LIB_FABRIC])
    CPPFLAGS=$saveCPPFLAGS
  fi
fi
OPTION_WITH([libibverbs], [LIBIBVERBS])
OPTION_WITH([librdmacm], [LIBRDMACM])
dnl next bits need to be outside 'if' or cray fails.
AM_CONDITIONAL([ENABLE_libibverbs], [test "$HAVE_libibverbs" = "yes"])
AM_CONDITIONAL([ENABLE_librdmacm], [test "$HAVE_librdmacm" = "yes"])

AC_DEFINE_UNQUOTED([HAVE_ZAP],["$have_zap"],[configured with zap transport (1) or not (0)])

have_auth=0
if test "$enable_ovis_auth" = "yes"; then
	have_auth=1
	AX_CHECK_OPENSSL([],[AC_MSG_ERROR([openssl not found, and required by ovis_auth])])
	AUTH_LIB="-lovis_auth"
else
	AUTH_LIB=""
fi
AC_SUBST([AUTH_LIB])
AC_SUBST([HAVE_AUTH],[$have_auth])
AC_DEFINE_UNQUOTED([HAVE_AUTH],[$have_auth],[configured with authentication (1) or not (0)])

OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])
OPTION_DEFAULT_DISABLE([ovis_event_test], [ENABLE_OVIS_EVENT_TEST])
OPTION_DEFAULT_DISABLE([ovis_ev_test], [ENABLE_OVIS_EV_TEST])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])



## stuff from ldms
OPTION_DEFAULT_ENABLE([scripts], [ENABLE_SCRIPTS])
OPTION_DEFAULT_DISABLE([slurmtest], [ENABLE_SLURMTEST])
OPTION_DEFAULT_DISABLE([developer], [ENABLE_DEVELOPER])
OPTION_DOC
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])
OPTION_DEFAULT_DISABLE([ldms-test], [ENABLE_LDMS_TEST])

OPTION_DEFAULT_ENABLE([mmap], [ENABLE_MMAP])
OPTION_DEFAULT_ENABLE([perf], [ENABLE_PERF])
OPTION_DEFAULT_DISABLE([yaml], [ENABLE_YAML])

dnl Options for store
OPTION_DEFAULT_ENABLE([store], [ENABLE_STORE])
OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])
OPTION_DEFAULT_DISABLE([rabbitkw], [ENABLE_RABBITKW])
OPTION_DEFAULT_DISABLE([rabbitv3], [ENABLE_RABBITV3])

dnl AMQP
OPTION_DEFAULT_DISABLE([amqp], [ENABLE_AMQP])

dnl Options for contributed stores
OPTION_DEFAULT_DISABLE([tutorial-store],[ENABLE_TUTORIAL_STORE])

AC_ARG_ENABLE([timescale-store],
        [AS_HELP_STRING([--enable-timescale-store], [enable timescaledb store plugin])],
        [],
        [enable_timescale_store="check"])
AS_IF([test "x$enable_timescale_store" != xno ],[
        AC_LIB_HAVE_LINKFLAGS([pq], [], [#include <libpq-fe.h>])
        AS_IF([test "x$enable_timescale_store" != xcheck],[
                AS_IF([test "x$HAVE_LIBPQ" = xno],
                        [AC_MSG_ERROR([libpq or headers not found])])
        ])
])
AM_CONDITIONAL([ENABLE_TIMESCALE_STORE], [test "x$HAVE_LIBPQ" = xyes])

OPTION_DEFAULT_DISABLE([gpcdlocal], [ENABLE_GPCDLOCAL],[Required access to gpcd-support repository])
OPTION_WITH_OR_BUILD([gpcdlocal],[../gpcd-support],[gpcd],[gpcdlocal.sh], ,[gpcd])
OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_WITH_OR_BUILD([sos],[../sos],[sos/src sos/include ods/src ods/include])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_DISABLE([kgnilnd], [ENABLE_KGNILND])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_DISABLE([jobid], [ENABLE_JOBID])
OPTION_DEFAULT_ENABLE([clock], [ENABLE_CLOCK])
OPTION_DEFAULT_ENABLE([synthetic], [ENABLE_SYNTHETIC])
OPTION_DEFAULT_ENABLE([varset], [ENABLE_VARSET])
OPTION_DEFAULT_ENABLE([lnet_stats], [ENABLE_LNET_STATS])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_ENABLE([coretemp], [ENABLE_CORETEMP])
OPTION_DEFAULT_DISABLE([filesingle], [ENABLE_FILESINGLE])
OPTION_DEFAULT_DISABLE([msr_interlagos], [ENABLE_MSR_INTERLAGOS])
OPTION_DEFAULT_ENABLE([array_example], [ENABLE_ARRAY_EXAMPLE])
OPTION_DEFAULT_ENABLE([hello_stream], [ENABLE_HELLO_STREAM])
OPTION_DEFAULT_ENABLE([blob_stream], [ENABLE_BLOB_STREAM])
OPTION_DEFAULT_DISABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_DISABLE([mpi_sampler], [ENABLE_MPI_SAMPLER])
OPTION_DEFAULT_DISABLE([mpi_noprofile], [ENABLE_MPI_NOPROFILE])
if test -z "$ENABLE_MPI_SAMPLER_TRUE" ; then
	AX_MPI([:],[ AC_MSG_ERROR([MPICC required by mpi_sampler ])])
fi
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnet], [ENABLE_PROCNET])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([dstat], [ENABLE_DSTAT])
OPTION_DEFAULT_ENABLE([procstat], [ENABLE_PROCSTAT])
OPTION_DEFAULT_ENABLE([llnl-edac], [ENABLE_LLNL_EDAC])
AM_CONDITIONAL([ENABLE_LLNL], [test "x$ENABLE_LLNL_EDAC_FALSE" = "x#"])
OPTION_DEFAULT_DISABLE([fptrans], [ENABLE_FPTRANS])

OPTION_DEFAULT_ENABLE([tsampler], [ENABLE_TSAMPLER])
OPTION_DEFAULT_ENABLE([cray_power_sampler], [ENABLE_CRAY_POWER_SAMPLER])
dnl cray_power_sampler won't build with --disable-tsampler

OPTION_DEFAULT_DISABLE([opa2], [ENABLE_OPA2])
OPTION_DEFAULT_DISABLE([ibnet], [ENABLE_IBNET])
OPTION_DEFAULT_ENABLE([loadavg], [ENABLE_LOADAVG])
OPTION_DEFAULT_ENABLE([vmstat], [ENABLE_VMSTAT])
OPTION_DEFAULT_ENABLE([procdiskstats], [ENABLE_PROCDISKSTATS])
OPTION_DEFAULT_DISABLE([cray_system_sampler], [ENABLE_CRAY_SYSTEM_SAMPLER])
dnl spaceless names in the cray_system_sampler. Default is with spaces for reverse compatibility
OPTION_DEFAULT_ENABLE([spaceless_names], [ENABLE_SPACELESS_NAMES])
dnl aries_mmr is not part of the cray_system_sampler
OPTION_DEFAULT_DISABLE([aries-mmr], [ENABLE_ARIES_MMR], [Requires --enable-gpcd or --with-aries-libgpcd=libdir,incdir])
dnl linkstatus is not part of the cray_system_sampler
OPTION_DEFAULT_DISABLE([aries_linkstatus], [ENABLE_ARIES_LINKSTATUS], [Requires gpcdr to be set up with status metrics])
OPTION_DEFAULT_DISABLE([atasmart], [ENABLE_ATASMART])
OPTION_DEFAULT_ENABLE([generic_sampler], [ENABLE_GENERIC_SAMPLER])
OPTION_DEFAULT_DISABLE([switchx], [ENABLE_SWITCHX])
OPTION_WITH([switchx], [SWITCHX],[/usr/local])
OPTION_DEFAULT_DISABLE([darshan], [ENABLE_DARSHAN])
OPTION_DEFAULT_DISABLE([kokkos], [ENABLE_KOKKOS])
OPTION_DEFAULT_ENABLE([jobinfo-sampler], [ENABLE_JOBINFO])
OPTION_DEFAULT_DISABLE([ibm_occ], [ENABLE_IBM_OCC_SAMPLER])
OPTION_DEFAULT_DISABLE([appinfo], [ENABLE_APPINFO])
OPTION_DEFAULT_DISABLE([app-sampler], [ENABLE_APP_SAMPLER])
OPTION_DEFAULT_DISABLE([store-app], [ENABLE_STORE_APP])
OPTION_DEFAULT_DISABLE([test_sampler], [ENABLE_TEST_SAMPLER])
OPTION_DEFAULT_DISABLE([list_sampler], [ENABLE_LIST_SAMPLER])
OPTION_DEFAULT_DISABLE([record_sampler], [ENABLE_RECORD_SAMPLER])
dnl test_sampler will also build with --enable-ldms-test
AM_CONDITIONAL([ENABLE_TEST_SAMPLER_LDMS_TEST], [test "x$ENABLE_LDMS_TEST_FALSE" = "x#" -o "x$ENABLE_TEST_SAMPLER_FALSE" = "x#"])
OPTION_DEFAULT_DISABLE([grptest], [ENABLE_GRPTEST])
dnl grptest will also build with --enable-ldms-test
AM_CONDITIONAL([ENABLE_GRPTEST_LDMS_TEST], [test "x$ENABLE_LDMS_TEST_FALSE" = "x#" -o "x$ENABLE_GRPTEST_FALSE" = "x#"])

dnl options for contributed samplers
OPTION_DEFAULT_DISABLE([ipmireader], [ENABLE_IPMIREADER])
OPTION_DEFAULT_DISABLE([tutorial-sampler],[ENABLE_TUTORIAL_SAMPLER])

dnl check for libcurl if influx is configured
OPTION_DEFAULT_DISABLE([influx], [ENABLE_INFLUX])
if test -z "$ENABLE_INFLUX_TRUE"; then
	TMPLIBS="$LIBS"
	LIBS=""
	AC_CHECK_LIB(curl, curl_version, [],
		AC_MSG_ERROR([libcurl not found (required by influx).]))
	AC_CHECK_HEADER(curl/curl.h, [],
		AC_MSG_ERROR([`curl.h` not found (required by influx).]))
	LIBS="$TMPLIBS"
fi

AC_ARG_ENABLE([papi],
	[AS_HELP_STRING([--enable-papi], [require components that depend upon libpapi (and libpfm) @<:@default=check@:>@])],
	[],
	[enable_papi="check"])
AS_IF([test "x$enable_papi" != xno],[
	AC_LIB_HAVE_LINKFLAGS([papi], [], [#include <papi.h>])
	AS_IF([test "x$enable_papi" != xcheck],[
		AS_IF([test "x$HAVE_LIBPAPI" = xno],
			[AC_MSG_ERROR([libpapi or headers not found])])
	])
	AC_LIB_HAVE_LINKFLAGS([pfm], [], [#include <perfmon/pfmlib_perf_event.h>])
	AS_IF([test "x$enable_papi" != xcheck],[
		AS_IF([test "x$HAVE_LIBPFM" = xno],
			[AC_MSG_ERROR([libpfm or headers not found])])
	])
])
AM_CONDITIONAL([HAVE_LIBPAPI], [test "x$HAVE_LIBPAPI" = xyes])
AM_CONDITIONAL([HAVE_LIBPFM], [test "x$HAVE_LIBPFM" = xyes])

AC_ARG_ENABLE([infiniband],
	[AS_HELP_STRING([--enable-infiniband], [require components that depend upon libibmad and libibumad @<:@default=check@:>@])],
	[],
	[enable_infiniband="check"])
AS_IF([test "x$enable_infiniband" != xno],[
	AC_LIB_HAVE_LINKFLAGS([ibmad], [], [#include <infiniband/mad.h>])
	AS_IF([test "x$enable_infiniband" != xcheck],[
		AS_IF([test "x$HAVE_LIBIBMAD" = xno],
			[AC_MSG_ERROR([libibmad or <infiniband/mad.h> not found])])
	])
	AC_LIB_HAVE_LINKFLAGS([ibumad], [], [#include <infiniband/umad.h>])
	AS_IF([test "x$enable_infiniband" != xcheck],[
		AS_IF([test "x$HAVE_LIBIBUMAD" = xno],
			[AC_MSG_ERROR([libibumad or <infiniband/umad.h> not found])])
	])
        CPPFLAGS_BACKUP=$CPPFLAGS
        CPPFLAGS="$CPPFLAGS -I/usr/include/infiniband"
        AC_CHECK_HEADERS([iba/ib_types.h], [], [
		AS_IF([test "x$enable_infiniband" != xcheck],
			[AC_MSG_ERROR([Header <infiniband/iba/ib_types.h> not found ])])
        ])
        CPPFLAGS=$CPPFLAGS_BACKUP
])
AM_CONDITIONAL([HAVE_LIBIBMAD], [test "x$HAVE_LIBIBMAD" = xyes])
AM_CONDITIONAL([HAVE_LIBIBUMAD], [test "x$HAVE_LIBIBUMAD" = xyes])
AM_CONDITIONAL([HAVE_IBA_IB_TYPES_H], [test "x$ac_cv_header_iba_ib_types_h" = xyes])

# enable tx2mon by default only when sensible.
AC_ARG_ENABLE([tx2mon],
	[AS_HELP_STRING([--enable-tx2mon], [require components that depend upon tx2mon header) @<:@default=check@:>@])],
	[],
	[enable_tx2mon="check"])
AS_IF([test "x$enable_tx2mon" != xno],[
	AC_CHECK_HEADER([tx2mon/mc_oper_region.h], [HAVE_TX2MON=yes; enable_tx2mon=yes], [HAVE_TX2MON=no], [#include <stdint.h>])
	AS_IF([test "x$enable_tx2mon" != xcheck],[
		AS_IF([test "x$HAVE_TX2MON" = xno], [AC_MSG_ERROR([tx2mon headers not found. Install tx2mon package and (at runtime) kernel module on ThunderX2 platforms.])])
	])
])
AM_CONDITIONAL([HAVE_TX2MON], [test "x$HAVE_TX2MON" = xyes])
AM_CONDITIONAL([ENABLE_TX2MON], [test "$enable_tx2mon" = "yes"])

if test -z "$ENABLE_KOKKOS_TRUE"; then
	dnl kokkos needs sos (not the storesos).
	CHECK_SOS=1
fi
if test -z "$ENABLE_DARSHAN_TRUE"; then
        dnl darshan needs sos (not the storesos).
        CHECK_SOS=1
fi
if test -z "$ENABLE_STORE_APP_TRUE"; then
	dnl kokkos needs sos (not the storesos).
	CHECK_SOS=1
fi

dnl Other libraries
OPTION_DEFAULT_DISABLE([gemini-gpcdr], [ENABLE_GEMINI_GPCDR]) dnl use gpcdr for gemini (not mutually exclusive)
OPTION_DEFAULT_DISABLE([aries-gpcdr], [ENABLE_ARIES_GPCDR]) dnl use gpcdr for aries (not mutually exclusive)

OPTION_DEFAULT_DISABLE([cray-nvidia], [ENABLE_CRAY_NVIDIA])
OPTION_DEFAULT_DISABLE([cray-nvidia-inc], [ENABLE_CRAY_NVIDIA_INC])
OPTION_WITH([cray-nvidia-inc], [CRAY_NVIDIA_INC]) dnl for the path to nvidia include

dnl Will need to have the NVIDIA library in the LD_LIBRARY_PATH
OPTION_DEFAULT_DISABLE([cray-hss-devel], [ENABLE_CRAY_HSS_DEVEL])
OPTION_WITH([cray-hss-devel], [CRAY_HSS_DEVEL],[/usr])

# for aries_mmr
OPTION_ARIES_MMR

dnl munge
OPTION_DEFAULT_DISABLE([munge], [ENABLE_MUNGE])
OPTION_WITH([munge], [MUNGE])

dnl OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
# OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[mmalloc third coll ovis_util zap ovis_ctrl ovis_event ovis_auth],[ovis-auth.sh],[ovis-lib],[ovis_auth])
dnl OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[mmalloc third json tada coll ovis_util zap ovis_ctrl ovis_event ovis_auth ],[ovis-auth.sh],[ovis-lib],[ovis_auth])
dnl substitute values we need from ovis-auth.sh during joint configure
AC_SUBST(OVIS_AUTH_LIBS)

LDMSD_PLUGIN_LIBPATH=${pkglibdir}
AC_SUBST(LDMSD_PLUGIN_LIBPATH)

dnl Check for pthread support
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
dnl save LIBS variable.
TMPLIBS="$LIBS"
LIBS=""

dnl no clock_gettime on osx. see
dnl https://developer.apple.com/library/mac/qa/qa1398/_index.html
AC_SEARCH_LIBS([clock_gettime],[rt posix4],[LDFLAGS_GETTIME=$LIBS])
LIBS=""
AC_SUBST([LDFLAGS_GETTIME])

dnl Check for libreadline
AX_LIB_READLINE
LDFLAGS_READLINE=$LIBS
AC_SUBST([LDFLAGS_READLINE])
LIBS=$TMPLIBS

dnl Munge library check
if test -z "$ENABLE_MUNGE_TRUE"; then
	AC_CHECK_LIB(munge, munge_encode, [], AC_MSG_ERROR([libmunge not found]))
	dnl Reset LIBS variable.
	LIBS=""

	AC_CHECK_HEADER(munge.h, [], AC_MSG_ERROR([munge.h not found]))
fi

AC_ARG_WITH([slurm],
	[AS_HELP_STRING([--with-slurm],
			[support SLURM jobid information and more @<:@default=check@:>@])],
			[AS_IF([test "x$withval" != xyes -a "x$withval" != xno],
				[SLURM_CFLAGS="-I $withval/include"])],
	[with_slurm=check])
have_slurm=no
AC_SUBST([SLURM_CFLAGS])
AS_IF([test "x$with_slurm" != no],[
	save_CFLAGS=$CFLAGS
	CFLAGS="$CFLAGS $SLURM_CFLAGS"
	AC_CHECK_HEADER([slurm/spank.h], [have_slurm=yes], [have_slurm=no])
	CFLAGS=$save_CFLAGS

	AS_IF([test "x$have_slurm=" = xno],[
		AS_IF([test "x$with_slurm" != xcheck],
			[AC_MSG_ERROR([<slurm/spank.h> not found. slurm installed?])],
			[AC_MSG_WARN([Disabling SLURM support.])])])
])
AM_CONDITIONAL([HAVE_SLURM], [test "x$have_slurm" = xyes])

AC_CHECK_HEADER([linux/netlink.h], [have_netlink=yes], [have_netlink=no])
AM_CONDITIONAL([HAVE_NETLINK], [test "x$have_netlink" = xyes])

if test -z "$ENABLE_SOS_TRUE"; then
	CHECK_SOS=1
fi

if test -n "$CHECK_SOS"; then
	if test "x$WITH_SOS" != "xbuild"; then
		OCFLAGS=$CFLAGS
		CFLAGS=$SOS_INCDIR_FLAG
		AC_CHECK_HEADERS(sos/sos.h,
			[],
			AC_MSG_FAILURE([sos/sos.h not found])
		)
		AC_CHECK_LIB( sos, sos_container_open,
			[],
			AC_MSG_FAILURE(libsos not found: required by either --enable-sos or --enable-kokkos),
			[ $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG ]
		)
		CFLAGS=$OCFLAGS
		LIBS=""
	else
		AC_MSG_NOTICE([Using staged ovis-sos $OVIS_SOS_LIB64DIR_FLAG $OVIS_SOS_LIBDIR_FLAG])
	fi
fi
if test -n "$SOS_LIB64DIR"; then
	soslibdir="$SOS_LIB64DIR"
	plugins_rpath="$plugins_rpath:$SOS_LIB64DIR"
fi
if test -n "$SOS_LIBDIR"; then
	soslibdir="$SOS_LIBDIR"
	plugins_rpath="$plugins_rpath:$SOS_LIBDIR"
fi
AC_SUBST(soslibdir)

if test -z "$ENABLE_ATASMART_TRUE" ; then
	AC_CHECK_LIB(atasmart, sk_disk_smart_read_data, [],
		AC_MSG_ERROR([libatasmart not found. sampler_atasmart requires libatasmart and libatasmart-devel.]))
fi


if test -z "$ENABLE_RABBITV3_TRUE" -o -z "$ENABLE_RABBITKW_TRUE"; then
	AC_CHECK_LIB(rabbitmq, amqp_version, [],
		AC_MSG_ERROR([librabbitmq not found. store_rabbitv3 requires librabbitmq and amqp headers.]))
	AC_LANG_PUSH([C])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#include <amqp.h>
#if ((AMQP_VERSION_MINOR < 7) && (AMQP_VERSION_MAJOR < 1))
#error "librabbitmq version < 0.7 not supported"
#endif
]])],
	[AC_DEFINE([HAVE_RABBITMQ],[],[Is librabbitmq >= 0.7.0 available?])],
	[ AC_MSG_ERROR([librabbitmq version > 0.7 not found])])
	AC_LANG_POP([C])
fi

if test -z "$ENABLE_YAML_TRUE"; then
	AC_CHECK_LIB(yaml, yaml_parser_load, [],
		AC_MSG_ERROR([libyaml not found. please add --disable-yaml to disable yaml support.]))
	dnl Reset LIBS variable.
	LIBS=""
fi

if test -z "$ENABLE_SOS_TRUE"; then
	if test "x$WITH_SOS" != "xbuild"; then
		OCFLAGS=$CFLAGS
		CFLAGS=$SOS_INCDIR_FLAG
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

AC_ARG_ENABLE([python],
	[AS_HELP_STRING([--disable-python], [disable the LDMS python API])],
	[want_python=$enableval],
	[want_python="check"])
dnl --disable-ldms-python is deprecated, and we should remove it in 5.X
AC_ARG_ENABLE([ldms-python],
	[AS_HELP_STRING([--disable-ldms-python], [disable the LDMS python API (deprecated)])],
	[want_python=$enableval])

AC_ARG_VAR([CYTHON], [Cython compiler command])

dnl ldms's python APIs and programs
dnl We make the assumption that if AM_PATH_PYTHON succeeds, that AX_PYTHON_DEVEL
dnl is very likely to succeed as well.
python_found=false
cython_found=false
AS_IF([test "x$want_python" != xno], [
	AM_PATH_PYTHON([3.6],
		[AX_PYTHON_DEVEL([>='3.6'])
		 pkgpythondir="${pythondir}/ovis_ldms"
		 pkgpyexecdir="${pkgpythondir}"
		 AX_PYTHON_MODULE_VERSION(["argparse"], 1.1, "python")
		 python_found=true
		 CYTHON_CHECK([0.25], [$PYTHON_VERSION],
		 	[cython_found=true], [cython_found=false])],
		[python_found=false
		 cython_found=false])

	AS_IF([test "$python_found" = false],
		[AS_IF([test "x$want_python" = xyes],
			[AC_MSG_ERROR([python not found])],
			[AC_MSG_WARN([python not found])])])
	AS_IF([test "$cython_found" = false],
		[AS_IF([test "x$want_python" = xyes],
			[AC_MSG_ERROR([cython not found])],
			[AC_MSG_WARN([cython not found])])])
])
AM_CONDITIONAL([ENABLE_PYTHON], [test "$python_found" = true -a "$cython_found" = true])

OPTION_WITH_PORT([LDMSD],[411])

dnl if cray_sampler set some other variables
if test "x$enable_cray_system_sampler" = "xyes"
then

	if test -z "$ENABLE_SPACELESS_NAMES_TRUE"; then
	   AC_DEFINE([HAVE_SPACELESS_NAMES],[1],[cray_system_sampler: Using spaceless names.])
	fi

	if !(test -z "$ENABLE_GEMINI_GPCDR_TRUE") && !(test -z "$ENABLE_ARIES_GPCDR_TRUE")
	then
		AC_MSG_ERROR([Must enable at least one of gemini-gpcdr, and aries-gpcdr with --enable-cray-system-sampler.])
	fi

	if test -z "$ENABLE_GEMINI_GPCDR_TRUE"
	then
	gpcdrfile="/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
	if ! test -f $gpcdrfile; then
	   AC_MSG_WARN([
	 ++++++++++++++++++++++++++++++++++++++++++++
	   GEMINI gpcdr interface not available:
	   $gpcdrfile
	   We hope your runtime environment has it.
	 ++++++++++++++++++++++++++++++++++++++++++++
	  ])
	  fi
	  AC_MSG_RESULT([Building gpcdr interface for GEMINI HSN metrics.])
	fi

	if test -z "$ENABLE_ARIES_GPCDR_TRUE"
	then
	gpcdrfile1="/sys/devices/virtual/gni/gpcdr0/metricsets/linktraffic/metrics"
	gpcdrfile2="/sys/devices/virtual/gni/gpcdr0/metricsets/linkstalled/metrics"
	gpcdrfile3="/sys/devices/virtual/gni/gpcdr0/metricsets/linksendstatus/metrics"
	gpcdrfile4="/sys/devices/virtual/gni/gpcdr0/metricsets/linkrecvstatus/metrics"
	if !(test -f $gpcdrfile1) || !(test -f $gpcdrfile2) || !(test -f $gpcdrfile3) || !(test -f $gpcdrfile4)
	then
	   AC_MSG_WARN([
	 ++++++++++++++++++++++++++++++++++++++++++++
	   ARIES gpcdr interface not available:
	   $gpcdrfile1 $gpcdrfile2 $gpcdrfile3 $gpcdrfile4
	   We hope your runtime environment has it.
	 ++++++++++++++++++++++++++++++++++++++++++++
	  ])
	  fi
	  AC_MSG_RESULT([Building gpcdr interface for ARIES HSN metrics.])
	fi

	if test -z "$ENABLE_CRAY_NVIDIA_TRUE"
	then
	  AC_DEFINE([HAVE_CRAY_NVIDIA],[1],[cray_system_sampler: Including option for NVIDIA metrics.])
	  AC_MSG_RESULT([cray_system_sampler: Including option for NVIDIA metrics.])
	else
	  AC_MSG_RESULT([cray_system_sampler: Not including option for NVIDIA metrics.])
	fi

	if test -z "$ENABLE_LUSTRE_TRUE"
	then
		AC_DEFINE([HAVE_LUSTRE],[1],[cray_system_sampler: Include option for LUSTRE metrics.])
		AC_MSG_RESULT([cray_system_sampler: Including option for LUSTRE metrics.])
	else
		AC_MSG_RESULT([cray_system_sampler: Not including option for LUSTRE metrics.])
	fi
fi

dnl Reset LIBS variable.

LIBS=""


## stuff from top
OPTION_DEFAULT_DISABLE([libgenders], [ENABLE_LIBGENDERS],[  requires C++,boost])
OPTION_DEFAULT_DISABLE([genderssystemd], [ENABLE_TOSS_SYSTEMD],[  toss systemd init scripts])
OPTION_EXTRA_BUILD_DIRS([third-plugins], [ENABLE_THIRD_PLUGINS], [THIRD_PLUGINS_DIRS])

if test -z "$ENABLE_LIBGENDERS_TRUE"
then
	OPTION_WITH([libgenders], [LIBGENDERS],[/usr])
	AC_LANG_PUSH([C++])
	BOOST_REQUIRE( , AC_MSG_ERROR([Genders enabled but boost not found.]))
	BOOST_STRING_ALGO
	save_LDFLAGS=$LDFLAGS
	LDFLAGS=$LIBGENDERS_LIBDIR_FLAG
	AX_CXX_CHECK_LIB(gendersplusplus,[Gendersplusplus::GendersException], [],
		AC_MSG_ERROR([libgendersplusplus not found. ]))
	LDFLAGS=$save_LDFLAGS
	AC_LANG_POP([C++])
	LIBS=""
fi

OPTION_GITINFO
OPTION_HOSTINFO

AC_ARG_WITH([dcgm],
  [AS_HELP_STRING([--with-dcgm],
    [Include code that depends on Nvidia DCGM @<:@default=check@:>@])],
  [],
  [with_dcgm=check])
LIBDCGM=
AS_IF([test "x$with_dcgm" != xno],
      [AC_CHECK_LIB([dcgm], [dcgmInit],
         [have_dcgm=true],
         [have_dcgm=false
          if test "x$with_dcgm" != xcheck; then
            AC_MSG_FAILURE([--with-dcgm was given, but test for dcgm failed])
          fi
         ])],
      [have_dcgm=false])
AM_CONDITIONAL([HAVE_DCGM], [test x$have_dcgm = xtrue])

# define substitutions for configvars and other sed-generated files.
# note carefully the escapes.
OVIS_DO_SUBST([LDMS_SUBST_RULE], ["sed \
-e 's,[[@]]VERSION[[@]],\$(VERSION),g' \
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
-e 's,[[@]]LDMSD_PLUGIN_LIBPATH[[@]],\$(LDMSD_PLUGIN_LIBPATH),g' \
-e 's,[[@]]ZAP_LIBPATH[[@]],\$(ZAP_LIBPATH),g' \
-e 's,[[@]]pkglibdir[[@]],\$(pkglibdir),g' \
-e 's,[[@]]plugins_rpath[[@]],\$(plugins_rpath),g' \
-e 's,[[@]]pythondir[[@]],\$(pythondir),g' \
-e 's,[[@]]SOS_LIB64DIR[[@]],\$(SOS_LIB64DIR),g' \
-e 's,[[@]]SOS_LIBDIR[[@]],\$(SOS_LIBDIR),g' \
"
])

AC_SUBST(ac_configure_args)
AC_SUBST(configure_input)

AC_CONFIG_FILES([Makefile
util/Makefile
util/relocation/Makefile
util/sample_init_scripts/Makefile
util/sample_init_scripts/genders/Makefile
util/sample_init_scripts/genders/man/Makefile
util/sample_init_scripts/genders/src/Makefile
util/sample_init_scripts/genders/systemd/Makefile
util/sample_init_scripts/genders/systemd/etc/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ClusterGenders/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ClusterSecrets/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/plugins-conf/Makefile
util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/debug/Makefile
util/sample_init_scripts/genders/systemd/services/Makefile
util/sample_init_scripts/genders/sysv/Makefile
util/sample_init_scripts/genders/sysv/etc/Makefile
util/sample_init_scripts/genders/sysv/etc/init.d/Makefile
util/sample_init_scripts/genders/examples/Makefile
lib/Doxyfile
lib/Makefile
lib/src/Makefile
lib/src/ovis_json/Makefile
lib/src/third/Makefile
lib/src/coll/Makefile
lib/src/mmalloc/Makefile
lib/src/ovis_ev/Makefile
lib/src/ovis_event/Makefile
lib/src/ovis_ctrl/Makefile
lib/src/ovis_util/Makefile
lib/src/ovis_auth/Makefile
lib/src/ovis_auth/ovis-auth.sh
lib/src/ovis_ref/Makefile
lib/src/zap/Makefile
lib/src/zap/rdma/Makefile
lib/src/zap/fabric/Makefile
lib/src/zap/sock/Makefile
lib/src/zap/ugni/Makefile
lib/src/zap/test/Makefile
lib/etc/Makefile
lib/etc/ovis/Makefile
lib/etc/ld.so.conf.d/Makefile
ldms/Makefile
ldms/src/Makefile
ldms/src/core/Makefile
ldms/src/auth/Makefile
ldms/src/store/Makefile
ldms/src/sampler/Makefile
ldms/src/contrib/Makefile
ldms/src/sampler/blob_stream/Makefile
ldms/src/store/stream/Makefile
ldms/src/store/kokkos/Makefile
ldms/src/store/darshan/Makefile
ldms/src/store/kokkos_appmon/Makefile
ldms/src/store/papi/Makefile
ldms/src/store/slurm/Makefile
ldms/src/store/influx/Makefile
ldms/src/store/store_flatfile/Makefile
ldms/src/store/store_app/Makefile
ldms/src/contrib/store/Makefile
ldms/src/contrib/store/tutorial/Makefile
ldms/src/contrib/store/timescale/Makefile
ldms/src/sampler/dcgm_sampler/Makefile
ldms/src/sampler/dstat/Makefile
ldms/src/sampler/hello_stream/Makefile
ldms/src/sampler/hello_stream/stream_configs/Makefile
ldms/src/sampler/filesingle/Makefile
ldms/src/sampler/lustre/Makefile
ldms/src/sampler/job_info/Makefile
ldms/src/sampler/job_info_slurm/Makefile
ldms/src/sampler/llnl/Makefile
ldms/src/sampler/lustre_client/Makefile
ldms/src/sampler/lustre_mdt/Makefile
ldms/src/sampler/lustre_ost/Makefile
ldms/src/sampler/shm/Makefile
ldms/src/sampler/shm/shm_util/Makefile
ldms/src/sampler/shm/mpi_profiler/Makefile
ldms/src/sampler/cray_system_sampler/Makefile
ldms/src/sampler/aries_mmr/Makefile
ldms/src/sampler/aries_mmr/aries_mmr_set_configs/Makefile
ldms/src/sampler/ibmad_sampler/Makefile
ldms/src/sampler/ibnet/Makefile
ldms/src/sampler/procnet/Makefile
ldms/src/sampler/rdc_sampler/Makefile
ldms/src/sampler/slurm/Makefile
ldms/src/sampler/netlink/Makefile
ldms/src/sampler/spank/Makefile
ldms/src/sampler/papi/Makefile
ldms/src/sampler/appinfo_lib/Makefile
ldms/src/sampler/kgnilnd/Makefile
ldms/src/sampler/tx2mon/Makefile
ldms/src/sampler/meminfo/Makefile
ldms/src/sampler/procinterrupts/Makefile
ldms/src/sampler/procnetdev2/Makefile
ldms/src/sampler/variable/Makefile
ldms/src/sampler/procstat/Makefile
ldms/src/sampler/procstat2/Makefile
ldms/src/sampler/loadavg/Makefile
ldms/src/sampler/vmstat/Makefile
ldms/src/sampler/coretemp/Makefile
ldms/src/sampler/examples/Makefile
ldms/src/sampler/examples/array_example/Makefile
ldms/src/sampler/examples/synthetic/Makefile
ldms/src/sampler/examples/test_sampler/Makefile
ldms/src/sampler/examples/list_sampler/Makefile
ldms/src/sampler/examples/record_sampler/Makefile
ldms/src/sampler/ibm_occ/Makefile
ldms/src/sampler/syspapi/Makefile
ldms/src/sampler/app_sampler/Makefile
ldms/src/contrib/sampler/Makefile
ldms/src/contrib/sampler/ipmireader/Makefile
ldms/src/contrib/sampler/tutorial/Makefile
ldms/src/third-plugins/Makefile
ldms/src/ldmsd/Makefile
ldms/src/ldmsd/test/Makefile
ldms/scripts/Makefile
ldms/src/test/Makefile
ldms/etc/Makefile
ldms/etc/systemd/Makefile
ldms/python/Makefile
ldms/python/ldmsd/Makefile
ldms/python/cmds/Makefile
ldms/Doxyfile
ldms/scripts/ldmsd
ldms/scripts/envldms.sh
ldms/man/Makefile
] )

AC_CONFIG_FILES([
	util/relocation/bld_manifest.sh
	util/relocation/relocate-paths.sh
	util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ldmsd
	util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ldms-functions
	util/sample_init_scripts/genders/systemd/ldmsd-pre-systemd
	util/sample_init_scripts/genders/systemd/ldmsd-wrapper.sh
	util/sample_init_scripts/genders/sysv/etc/init.d/ldmsd
	util/sample_init_scripts/genders/sysv/etc/init.d/ldmsd.template
	util/sample_init_scripts/genders/sysv/ldmsd-pre-sysvinit
	ldms/scripts/ldms-wrapper
	ldms/scripts/ldms-static-test.sh
	ldms/scripts/pll-ldms-static-test.sh
	ldms/scripts/ldms_ban.sh
	ldms/scripts/ldms-plugins.sh
	ldms/scripts/ldms-py-subset_test.sh
	ldms/scripts/ldms-py-varset.sh
	ldms/scripts/ldms-py-syslog.sh
	ldms/scripts/ldms-py-rename.sh
	ldms/scripts/ldms-meminfo.sh
	ldms/scripts/ldms_local_opa2test.sh
	ldms/scripts/ldms-l2_test.sh
	ldms/src/test/ldms-run-static-tests.test
	ldms/src/test/ldms-static-test-list.sh
	ldms/src/test/ldms-static-test-bypass
	], [chmod a+x $ac_file]
)

AC_OUTPUT

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
