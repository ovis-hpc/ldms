dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([ovis-ldms], 4.100.0, [ovis-help@sandia.gov])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([m4/Ovis-top.m4])
AC_CONFIG_AUX_DIR([config])
AM_CONFIG_HEADER([config.h])
AC_PROG_CC
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
AC_PROG_CC_C_O

dnl if LEX and/or YACC are not available, the configure still won't fail
AC_PROG_LEX
AC_PROG_YACC

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

OPTION_DEFAULT_ENABLE([python], [ENABLE_PYTHON])
if test -z "$ENABLE_PYTHON_TRUE"; then
	dnl check for python interpreter
	test -n "$PYTHON" || export PYTHON=/usr/bin/python3
	AM_PATH_PYTHON([3.6])
	AX_PYTHON_DEVEL([>='3.6'])
	#pkgpythondir="${pythondir}/ldms"
	#pkgpyexecdir="${pkgpythondir}"
fi

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_FLEXIBLE_ARRAY_MEMBER

AC_ARG_WITH([ovis-lib],
	    [AS_HELP_STRING([--with-ovis-lib],[Top level configure must not specify this option.])],
	    [AC_MSG_ERROR([Top level configure must not specify --with-ovis-lib option.])],
	    [])

dnl change sharedstatedir default
test "$sharedstatedir" = '${prefix}/com' && sharedstatedir='${prefix}/var/lib'

# ldms is not optional for now.
OPTION_DEFAULT_ENABLE([ldms], [ENABLE_LDMS],[  requires lib])

SUBST_MAYBE([ldms])

OPTION_DEFAULT_DISABLE([libgenders], [ENABLE_LIBGENDERS],[  requires C++,boost])
OPTION_DEFAULT_DISABLE([genderssystemd], [ENABLE_TOSS_SYSTEMD],[  toss systemd init scripts])
OPTION_WITH([libgenders], [LIBGENDERS],[/usr])

OVIS_PKGLIBDIR
OPTION_GITINFO
OPTION_HOSTINFO
OPTION_DOC
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])


OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_WITH([sos],[SOS])

# ==== lib ==== #

AC_SEARCH_LIBS([clock_gettime],[rt posix4],[LDFLAGS_GETTIME=$LIBS])
AC_SUBST([LDFLAGS_GETTIME])
LIBS=""

OPTION_DEFAULT_ENABLE([coll], [ENABLE_COLL])
OPTION_DEFAULT_ENABLE([ovis_event], [ENABLE_OVIS_EVENT])
OPTION_DEFAULT_ENABLE([mmalloc], [ENABLE_MMALLOC])
OPTION_DEFAULT_ENABLE([ovis_ctrl], [ENABLE_OVIS_CTRL])
OPTION_DEFAULT_ENABLE([ovis_util], [ENABLE_OVIS_UTIL])
OPTION_DEFAULT_ENABLE([ovis_auth], [ENABLE_OVIS_AUTH])
OPTION_DEFAULT_ENABLE([zap], [ENABLE_ZAP])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_DISABLE([fabric], [ENABLE_FABRIC])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])

OPTION_WITH([libibverbs], [LIBIBVERBS])
OPTION_WITH([librdmacm], [LIBRDMACM])
dnl next bits need to be outside 'if' or cray fails.
AM_CONDITIONAL([ENABLE_libibverbs], [test "$HAVE_libibverbs" = "yes"])
AM_CONDITIONAL([ENABLE_librdmacm], [test "$HAVE_librdmacm" = "yes"])

OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([ssl], [ENABLE_SSL])
OPTION_DEFAULT_DISABLE([zaptest], [ENABLE_ZAPTEST])
OPTION_DEFAULT_DISABLE([ovis_event_test], [ENABLE_OVIS_EVENT_TEST])


# ------------------ lib (end) ----------------------- #



# ==== ldms (begin) ==== #

OPTION_DEFAULT_ENABLE([ldms-python], [ENABLE_LDMS_PYTHON])
OPTION_DEFAULT_ENABLE([scripts], [ENABLE_SCRIPTS])
OPTION_DEFAULT_DISABLE([slurmtest], [ENABLE_SLURMTEST])
OPTION_DEFAULT_DISABLE([developer], [ENABLE_DEVELOPER])
OPTION_DEFAULT_DISABLE([ldms-test], [ENABLE_LDMS_TEST])
OPTION_DEFAULT_ENABLE([core], [ENABLE_CORE])
OPTION_DEFAULT_ENABLE([mmap], [ENABLE_MMAP])
OPTION_DEFAULT_ENABLE([perf], [ENABLE_PERF])
OPTION_DEFAULT_ENABLE([sensors], [ENABLE_SENSORS])
OPTION_DEFAULT_DISABLE([yaml], [ENABLE_YAML])

dnl Options for store
OPTION_DEFAULT_ENABLE([store], [ENABLE_STORE])
OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])
OPTION_DEFAULT_DISABLE([rabbitkw], [ENABLE_RABBITKW])
OPTION_DEFAULT_DISABLE([rabbitv3], [ENABLE_RABBITV3])
OPTION_DEFAULT_DISABLE([store-test], [ENABLE_STORE_TEST])
# NOTE: store-sos is enabled if `--enable-sos` is given

dnl AMQP
OPTION_DEFAULT_DISABLE([amqp], [ENABLE_AMQP])

OPTION_DEFAULT_DISABLE([gpcdlocal], [ENABLE_GPCDLOCAL],[Required access to gpcd-support repository])
OPTION_WITH([gpcdlocal],[gpcd])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_DISABLE([kgnilnd], [ENABLE_KGNILND])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_DISABLE([jobid], [ENABLE_JOBID])
OPTION_DEFAULT_ENABLE([clock], [ENABLE_CLOCK])
OPTION_DEFAULT_ENABLE([synthetic], [ENABLE_SYNTHETIC])
OPTION_DEFAULT_ENABLE([varset], [ENABLE_VARSET])
OPTION_DEFAULT_DISABLE([ipmireader], [ENABLE_IPMIREADER])
OPTION_DEFAULT_ENABLE([lnet_stats], [ENABLE_LNET_STATS])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_DISABLE([filesingle], [ENABLE_FILESINGLE])
OPTION_DEFAULT_DISABLE([msr_interlagos], [ENABLE_MSR_INTERLAGOS])
OPTION_DEFAULT_ENABLE([array_example], [ENABLE_ARRAY_EXAMPLE])
OPTION_DEFAULT_DISABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_DISABLE([rapl], [ENABLE_RAPL])
OPTION_DEFAULT_DISABLE([hweventpapi], [ENABLE_HWEVENTPAPI])
OPTION_DEFAULT_DISABLE([mpi_sampler], [ENABLE_MPI_SAMPLER])
OPTION_DEFAULT_DISABLE([mpi_noprofile], [ENABLE_MPI_NOPROFILE])
OPTION_WITH([libpapi], [LIBPAPI])
OPTION_WITH([libpfm], [LIBPFM])
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([procsensors], [ENABLE_PROCSENSORS])
OPTION_DEFAULT_ENABLE([dstat], [ENABLE_DSTAT])
OPTION_DEFAULT_ENABLE([procstat], [ENABLE_PROCSTAT])
OPTION_DEFAULT_ENABLE([llnl-edac], [ENABLE_LLNL_EDAC])
AM_CONDITIONAL([ENABLE_LLNL], [test "x$ENABLE_LLNL_EDAC_FALSE" = "x#"])
OPTION_DEFAULT_DISABLE([fptrans], [ENABLE_FPTRANS])

OPTION_DEFAULT_ENABLE([tsampler], [ENABLE_TSAMPLER])
OPTION_DEFAULT_ENABLE([cray_power_sampler], [ENABLE_CRAY_POWER_SAMPLER])
dnl cray_power_sampler won't build with --disable-tsampler

OPTION_DEFAULT_DISABLE([sysclassib], [ENABLE_SYSCLASSIB])
OPTION_DEFAULT_DISABLE([opa2], [ENABLE_OPA2])
OPTION_DEFAULT_ENABLE([vmstat], [ENABLE_VMSTAT])
OPTION_DEFAULT_ENABLE([sysclassblock], [ENABLE_SYSCLASSBLOCK])
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
OPTION_DEFAULT_DISABLE([kokkos], [ENABLE_KOKKOS])
OPTION_DEFAULT_ENABLE([jobinfo-sampler], [ENABLE_JOBINFO])
OPTION_DEFAULT_DISABLE([jobinfo-slurm], [ENABLE_SLURM_JOBINFO])
OPTION_DEFAULT_DISABLE([spank-plugin], [ENABLE_SPANK_PLUGIN])
OPTION_DEFAULT_ENABLE([slurm-sampler], [ENABLE_SLURM_SAMPLER])
OPTION_DEFAULT_DISABLE([papi-sampler], [ENABLE_PAPI_SAMPLER])
OPTION_DEFAULT_DISABLE([syspapi-sampler], [ENABLE_SYSPAPI_SAMPLER])
OPTION_DEFAULT_DISABLE([ibm_occ], [ENABLE_IBM_OCC_SAMPLER])
OPTION_DEFAULT_DISABLE([appinfo], [ENABLE_APPINFO])
OPTION_DEFAULT_DISABLE([test_sampler], [ENABLE_TEST_SAMPLER])
OPTION_DEFAULT_DISABLE([app_sampler], [ENABLE_APP_SAMPLER])

dnl test_sampler will also build with --enable-ldms-test
AM_CONDITIONAL([ENABLE_TEST_SAMPLER_LDMS_TEST], [test "x$ENABLE_LDMS_TEST_FALSE" = "x#" -o "x$ENABLE_TEST_SAMPLER_FALSE" = "x#"])
OPTION_DEFAULT_DISABLE([grptest], [ENABLE_GRPTEST])
dnl grptest will also build with --enable-ldms-test
AM_CONDITIONAL([ENABLE_GRPTEST_LDMS_TEST], [test "x$ENABLE_LDMS_TEST_FALSE" = "x#" -o "x$ENABLE_GRPTEST_FALSE" = "x#"])

dnl Options for translator
OPTION_DEFAULT_ENABLE([translator], [ENABLE_TRANSLATOR])
OPTION_DEFAULT_ENABLE([scripting_translator], [ENABLE_SCRIPTING_TRANSLATOR])
OPTION_DEFAULT_ENABLE([json_translator], [ENABLE_JSON_TRANSLATOR])
dnl End of translator plugins

OPTION_WITH([libcurl], [LIBCURL])
dnl check for libcurl if influx is configured
OPTION_DEFAULT_DISABLE([influx], [ENABLE_INFLUX])
OPTION_DEFAULT_ENABLE([ldmsd], [ENABLE_LDMSD])


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

dnl substitute values we need from ovis-auth.sh during joint configure
AC_SUBST(OVIS_AUTH_LIBS)

LDMSD_PLUGIN_LIBPATH=${pkglibdir}
AC_SUBST(LDMSD_PLUGIN_LIBPATH)

OPTION_WITH_PORT([LDMSD],[411])
OPTION_WITH_MAGIC([LDMS_MSG_MAX],[32768],[Maximum control message length allowed to ldmsd (min 4095)])

# ---- ldms (end) ---- #

# ==== library checking ==== #

dnl Check for pthread support
TMPLIBS="$LIBS" # save LIBS
LIBS=""
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
LIBS=$TMPLIBS # recover LIBS

dnl Check for libreadline
TMPLIBS="$LIBS" # save LIBS
LIBS=""
AX_LIB_READLINE
LDFLAGS_READLINE=$LIBS
LIBS=$TMPLIBS # recover LIBS
AC_SUBST([LDFLAGS_READLINE])

dnl Reset LIBS variable.
LIBS=""
if test -z "$ENABLE_LIBGENDERS_TRUE"
then
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

# --- zap --- #
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
AC_DEFINE_UNQUOTED([HAVE_ZAP],["$have_zap"],[configured with zap transport (1) or not (0)])
ZAP_LIBPATH=${pkglibdir}
ZAP_LIBPATH=${ZAP_LIBPATH#*/rpm7/BUILDROOT}
ZAP_LIBPATH=${ZAP_LIBPATH#*/tmproot}
AC_SUBST(ZAP_LIBPATH)

# --- auth / ssl --- #
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

# --- mpi --- #
if test -z "$ENABLE_MPI_SAMPLER_TRUE" ; then
	AX_MPI([:], AC_MSG_ERROR([MPICC required by mpi_sampler ]))
fi

# --- libpapi check --- #
if test -z "$ENABLE_SYSPAPI_SAMPLER_TRUE" -o -z "$ENABLE_PAPI_SAMPLER_TRUE"; then
	TMPLIBS="$LIBS"
	TMPCFLAGS="$CFLAGS"
	LIBS=""
	AC_CHECK_LIB(papi, PAPI_library_init, [],
		AC_MSG_ERROR([libpapi not found (required by papi-sampler/syspapi-sampler).]),
		$LIBPAPI_LIBDIR_FLAG $LIBPAPI_LIB64DIR_FLAG)
	LIBS=""
	dnl Check headers
	CFLAGS=$LIBPAPI_INCDIR_FLAG
	AC_CHECK_HEADER(papi.h, [],
		AC_MSG_ERROR([`papi.h` not found (required by papi-sampler/syspapi-sampler).]))
	LIBS="$TMPLIBS"
	CFLAGS="$TMPCFLAGS"
fi

if test -z "$ENABLE_SYSPAPI_SAMPLER_TRUE"; then
	TMPLIBS="$LIBS"
	TMPCFLAGS="$CFLAGS"
	LIBS=""
	AC_CHECK_LIB(pfm, pfm_initialize, [],
		AC_MSG_ERROR([libpfm not found (required by syspapi-sampler).]),
		$LIBPFM_LIBDIR_FLAG $LIBPFM_LIB64DIR_FLAG)
	LIBS=""
	dnl Check headers
	CFLAGS=$LIBPFM_INCDIR_FLAG
	AC_CHECK_HEADER(perfmon/pfmlib_perf_event.h, [],
		AC_MSG_ERROR([`perfmon/pfmlib_perf_event.h` not found (required by syspapi-sampler).]))
	LIBS="$TMPLIBS"
	CFLAGS="$TMPCFLAGS"
fi

# --- store_influx --- #
if test -z "$ENABLE_INFLUX_TRUE"; then
	TMPLIBS="$LIBS"
	TMPCFLAGS="$CFLAGS"
	LIBS=""
	AC_CHECK_LIB(curl, curl_version, [],
		AC_MSG_ERROR([libcurl not found (required by influx).]),
		$LIBCURL_LIBDIR_FLAG $LIBCURL_LIB64DIR_FLAG)
	CFLAGS=$LIBCURL_INCDIR_FLAG
	AC_CHECK_HEADER(curl/curl.h, [],
		AC_MSG_ERROR([`curl.h` not found (required by influx).]))
	LIBS="$TMPLIBS"
	CFLAGS="$TMPCFLAGS"
fi

dnl CHECK_SOS check SOS library
if test -z "$ENABLE_SOS_TRUE" ||
   test -z "$ENABLE_KOKKOS_TRUE"
then
	test -z "$ENABLE_PYTHON_TRUE" || AC_MSG_ERROR("python is disabled")
	# FIXME: Map.pyx is enabled under `--enable-sos`
	which cython >/dev/null 2>&1 || AC_MSG_ERROR("cython not found")
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
fi

dnl Munge library check
if test -z "$ENABLE_MUNGE_TRUE"; then
	AC_CHECK_LIB(munge, munge_encode, [], AC_MSG_ERROR([libmunge not found]))
	dnl Reset LIBS variable.
	LIBS=""

	AC_CHECK_HEADER(munge.h, [], AC_MSG_ERROR([munge.h not found]))
fi

dnl Checks for libraries
if test -z "$ENABLE_SYSCLASSIB_TRUE"; then
	AC_CHECK_LIB(ibmad, mad_rpc_open_port, [],
		AC_MSG_ERROR([mad_rpc_open_port() function not found. sysclassib sampler
		  requires libibmad 1.3.]))
	dnl Reset LIBS variable.
	LIBS=""

	AC_CHECK_HEADER(infiniband/mad.h, [],
		AC_MSG_ERROR([<infiniband/mad.h> not found.  Is libibmad-dev or
			libibmad-devel installed?]))

	AC_CHECK_LIB(ibumad, umad_get_port, [],
		AC_MSG_ERROR([umad_get_port() function not found. sysclassib sampler
			requires libibumad.]))
	dnl Reset LIBS variable.
	LIBS=""

	AC_CHECK_HEADER(infiniband/umad.h, [],
		AC_MSG_ERROR([<infiniband/umad.h> not found.  Is libibumad-dev or
			libibumad-devel installed?]))
fi

dnl Checks for slurm/spank for spank plugins
OPTION_WITH([slurm], [SLURM], [/usr])
if test -z "$ENABLE_SLURM_JOBINFO_TRUE" ||
   test -z "$ENABLE_SPANK_PLUGIN_TRUE" ; then
	OCFLAGS=$CFLAGS
	CFLAGS=$SLURM_INCDIR_FLAG
	AC_CHECK_HEADER(slurm/spank.h, [],
		AC_MSG_ERROR([<slurm/spank.h> not found.
			slurm installed?]))
	CFLAGS=$OCFLAGS
fi

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

dnl if cray_sampler set some other variables
if test "x$enable_cray_system_sampler" = "xyes"
then

	if test -z "$ENABLE_SPACELESS_NAMES_TRUE"; then
	   AC_DEFINE([HAVE_SPACELESS_NAMES],[1],[cray_system_sampler: Using spaceless names.])
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

# define substitutions for configvars and other sed-generated files.
# note carefully the escapes.
OVIS_DO_SUBST([OVIS_SUBST_RULE], ["sed \
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
-e 's,[[@]]pkglibdir[[@]],\$(pkglibdir),g' \
-e 's,[[@]]pythondir[[@]],\$(pythondir),g' \
-e 's,[[@]]ZAP_LIBPATH[[@]],\$(ZAP_LIBPATH),g' \
-e 's,[[@]]LDMSD_PLUGIN_LIBPATH[[@]],\$(LDMSD_PLUGIN_LIBPATH),g' \
-e 's,[[@]]SOS_LIB64DIR[[@]],\$(SOS_LIB64DIR),g' \
-e 's,[[@]]SOS_LIBDIR[[@]],\$(SOS_LIBDIR),g' \
"
])

AC_SUBST(ac_configure_args)

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
lib/ovis-lib.spec
lib/Doxyfile
lib/Makefile
lib/src/Makefile
lib/src/ev/Makefile
lib/src/json/Makefile
lib/src/third/Makefile
lib/src/coll/Makefile
lib/src/mmalloc/Makefile
lib/src/ovis_event/Makefile
lib/src/ovis_ctrl/Makefile
lib/src/ovis_util/Makefile
lib/src/ovis_auth/Makefile
lib/src/ovis_auth/ovis-auth.sh
lib/src/zap/Makefile
lib/src/zap/rdma/Makefile
lib/src/zap/fabric/Makefile
lib/src/zap/sock/Makefile
lib/src/zap/ugni/Makefile
lib/src/zap/test/Makefile
lib/bin/Makefile
lib/bin/src/Makefile
lib/python/Makefile
lib/etc/Makefile
lib/etc/ovis/Makefile
lib/etc/ld.so.conf.d/Makefile
ldms/Makefile
ldms/src/Makefile
ldms/src/core/Makefile
ldms/src/auth/Makefile
ldms/src/ldmsd/Makefile
ldms/src/ldmsd/test/Makefile
ldms/src/ldmsd-samplers/Makefile
ldms/src/ldmsd-samplers/all_example/Makefile
ldms/src/ldmsd-samplers/cray_power_sampler/Makefile
ldms/src/ldmsd-samplers/fptrans/Makefile
ldms/src/ldmsd-samplers/generic_sampler/Makefile
ldms/src/ldmsd-samplers/kgnilnd/Makefile
ldms/src/ldmsd-samplers/meminfo/Makefile
ldms/src/ldmsd-samplers/test-samplers/Makefile
ldms/src/ldmsd-samplers/sysclassblock/Makefile
ldms/src/ldmsd-samplers/procinterrupts/Makefile
ldms/src/ldmsd-samplers/procnetdev/Makefile
ldms/src/ldmsd-samplers/procnfs/Makefile
ldms/src/ldmsd-samplers/procstat/Makefile
ldms/src/ldmsd-samplers/sampler_atasmart/Makefile
ldms/src/ldmsd-samplers/synthetic/Makefile
ldms/src/ldmsd-samplers/vmstat/Makefile
ldms/src/ldmsd-samplers/lnet_stats/Makefile
ldms/src/ldmsd-samplers/lustre/Makefile
ldms/src/ldmsd-samplers/switchx/Makefile
ldms/src/ldmsd-samplers/sysclassib/Makefile
ldms/src/ldmsd-samplers/llnl/Makefile
ldms/src/ldmsd-samplers/aries_linkstatus/Makefile
ldms/src/ldmsd-samplers/aries_mmr/Makefile
ldms/src/ldmsd-samplers/cray_system_sampler/Makefile
ldms/src/ldmsd-samplers/msr_interlagos/Makefile
ldms/src/ldmsd-samplers/procsensors/Makefile
ldms/src/ldmsd-samplers/perfevent/Makefile
ldms/src/ldmsd-samplers/opa2/Makefile
ldms/src/ldmsd-samplers/appinfo/Makefile
ldms/src/ldmsd-samplers/variable/Makefile
ldms/src/ldmsd-samplers/dstat/Makefile
ldms/src/ldmsd-samplers/jobinfo/Makefile
ldms/src/ldmsd-samplers/jobinfo/jobinfo_slurm/Makefile
ldms/src/ldmsd-samplers/slurm/Makefile
ldms/src/ldmsd-samplers/slurm/spank/Makefile
ldms/src/ldmsd-samplers/shm/Makefile
ldms/src/ldmsd-samplers/shm/shm_util/Makefile
ldms/src/ldmsd-samplers/shm/mpi_profiler/Makefile
ldms/src/ldmsd-samplers/filesingle/Makefile
ldms/src/ldmsd-samplers/papi/Makefile
ldms/src/ldmsd-samplers/syspapi/Makefile
ldms/src/ldmsd-samplers/app_sampler/Makefile
ldms/src/ldmsd-stores/Makefile
ldms/src/ldmsd-stores/store_sos/Makefile
ldms/src/ldmsd-stores/store_papi/Makefile
ldms/src/ldmsd-stores/store_slurm/Makefile
ldms/src/ldmsd-stores/store_csv/Makefile
ldms/src/ldmsd-stores/store_amqp/Makefile
ldms/src/ldmsd-stores/store_test/Makefile
ldms/src/ldmsd-stores/store_influx/Makefile
ldms/src/ldmsd-stores/store_app/Makefile
ldms/scripts/Makefile
ldms/src/test/Makefile
ldms/src/ldmsd-translators/Makefile
ldms/src/ldmsd-translators/scripting/Makefile
ldms/src/ldmsd-translators/json-translator/Makefile
ldms/etc/Makefile
ldms/etc/systemd/Makefile
ldms/python/Makefile
ldms/python/ldmsd/Makefile
ldms/python/cmds/Makefile
ldms/scripts/ldmsd
ldms/scripts/envldms.sh
ldms/ldms.spec
ldms/man/Makefile
ldms/doc/Makefile
ldms/doc/Doxyfile
])

OVIS_EXEC_SCRIPTS(
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
)

AC_CONFIG_COMMANDS([scripts],[ echo fixing script perms.
for i in $ovis_exec_scripts; do
	if test -f $ovis_exec_scripts_objdir/$i; then
		chmod a+x $ovis_exec_scripts_objdir/$i;
	else
		echo failing chmod on missing $ovis_exec_scripts_objdir/$i;
	fi
done
],[
	ovis_exec_scripts="$ovis_exec_scripts"
	ovis_extra_dist="$ovis_extra_dist"
	ovis_exec_scripts_objdir="$ac_pwd"
])

AC_OUTPUT

if test "x$enable_rpath" != "xyes"; then
echo "TURNING libtool rpath into no-ops"
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool
fi
