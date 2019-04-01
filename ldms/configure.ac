dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis-ldms, 3.4.12, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/core/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([foreign tar-pax])
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])
])
AX_PREFIX_CONFIG_H_G(src/ovis-ldms-config.h,,,[
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

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long double)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_FLEXIBLE_ARRAY_MEMBER

dnl change sharedstatedir default
test "$sharedstatedir" = '${prefix}/com' && sharedstatedir='${prefix}/var/lib'

OVIS_PKGLIBDIR
OPTION_DEFAULT_ENABLE([scripts], [ENABLE_SCRIPTS])
OPTION_DEFAULT_DISABLE([slurmtest], [ENABLE_SLURMTEST])
OPTION_DEFAULT_DISABLE([developer], [ENABLE_DEVELOPER])
OPTION_DEFAULT_DISABLE([mmdebug], [ENABLE_MM_DEBUG])
if test "$enable_mmdebug" == "yes"; then
	AC_DEFINE([MM_DEBUG],[1],
		["enable mmalloc debug (and disable mmap) to be controlled by env(LDMS_ENABLE_MMALLOC_DEBUG)"])
fi
OPTION_DOC
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

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

dnl AMQP
OPTION_DEFAULT_DISABLE([amqp], [ENABLE_AMQP])

OPTION_DEFAULT_DISABLE([gpcdlocal], [ENABLE_GPCDLOCAL],[Required access to gpcd-support repository])
OPTION_WITH_OR_BUILD([gpcdlocal],[../gpcd-support],[gpcd],[gpcdlocal.sh], ,[gpcd])
OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_WITH_OR_BUILD([sos],[../sos],[sos/src sos/include ods/src ods/include])

dnl Options for consumer
OPTION_DEFAULT_DISABLE([me], [ENABLE_ME])
OPTION_WITH([me], [ME], [/usr/local])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_DISABLE([kgnilnd], [ENABLE_KGNILND])
OPTION_DEFAULT_DISABLE([knc_sampler], [ENABLE_KNC_SAMPLER])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_DISABLE([jobid], [ENABLE_JOBID])
OPTION_DEFAULT_ENABLE([clock], [ENABLE_CLOCK])
OPTION_DEFAULT_ENABLE([synthetic], [ENABLE_SYNTHETIC])
OPTION_DEFAULT_ENABLE([lnet_stats], [ENABLE_LNET_STATS])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_DISABLE([filesingle], [ENABLE_FILESINGLE])
OPTION_DEFAULT_DISABLE([msr_interlagos], [ENABLE_MSR_INTERLAGOS])
OPTION_DEFAULT_ENABLE([array_example], [ENABLE_ARRAY_EXAMPLE])
OPTION_DEFAULT_DISABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_DISABLE([rapl], [ENABLE_RAPL])
OPTION_DEFAULT_DISABLE([papi], [ENABLE_PAPI])
OPTION_WITH([libpapi], [LIBPAPI])
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([procsensors], [ENABLE_PROCSENSORS])
OPTION_DEFAULT_ENABLE([dstat], [ENABLE_DSTAT])
OPTION_DEFAULT_ENABLE([procstat], [ENABLE_PROCSTAT])
OPTION_DEFAULT_ENABLE([llnl-edac], [ENABLE_LLNL_EDAC])
OPTION_DEFAULT_ENABLE([llnl-lustre], [ENABLE_LLNL_LUSTRE])
AM_CONDITIONAL([ENABLE_LLNL], [test "x$ENABLE_LLNL_EDAC_FALSE" = "x#" -o "x$ENABLE_LLNL_LUSTRE_FALSE" = "x#"])
OPTION_DEFAULT_DISABLE([fptrans], [ENABLE_FPTRANS])

OPTION_DEFAULT_DISABLE([tsampler], [ENABLE_TSAMPLER])
OPTION_DEFAULT_ENABLE([cray_power_sampler], [ENABLE_CRAY_POWER_SAMPLER])
dnl cray_power_sampler won't build with --disable-tsampler

dnl power_sampler with powerapi
dnl power_sampler won't build with --disable-tsampler
OPTION_DEFAULT_DISABLE([pow], [ENABLE_POW])
OPTION_WITH([pow], [POW], [/usr/local])


OPTION_DEFAULT_DISABLE([sysclassib], [ENABLE_SYSCLASSIB])
OPTION_DEFAULT_DISABLE([opa2], [ENABLE_OPA2])
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
OPTION_DEFAULT_DISABLE([hadoop], [ENABLE_HADOOP])
OPTION_DEFAULT_ENABLE([generic_sampler], [ENABLE_GENERIC_SAMPLER])
OPTION_DEFAULT_DISABLE([switchx], [ENABLE_SWITCHX])
OPTION_WITH([switchx], [SWITCHX],[/usr/local])


OPTION_DEFAULT_ENABLE([ldmsd], [ENABLE_LDMSD])

dnl Other libraries
OPTION_DEFAULT_DISABLE([gemini-gpcdr], [ENABLE_GEMINI_GPCDR]) dnl use gpcdr for gemini (not mutually exclusive)
OPTION_DEFAULT_DISABLE([aries-gpcdr], [ENABLE_ARIES_GPCDR]) dnl use gpcdr for aries (not mutually exclusive)

OPTION_DEFAULT_DISABLE([cray-nvidia], [ENABLE_CRAY_NVIDIA])
OPTION_DEFAULT_DISABLE([cray-nvidia-inc], [ENABLE_CRAY_NVIDIA_INC])
OPTION_WITH([cray-nvidia-inc], [CRAY_NVIDIA_INC]) dnl for the path to nvidia include

dnl Will need to have the NVIDIA library in the LD_LIBRARY_PATH
OPTION_DEFAULT_DISABLE([rca], [ENABLE_RCA])
OPTION_WITH([rca], [RCA],[/usr])
OPTION_DEFAULT_DISABLE([krca], [ENABLE_KRCA])
OPTION_WITH([krca], [KRCA],[/usr])
OPTION_DEFAULT_DISABLE([cray-hss-devel], [ENABLE_CRAY_HSS_DEVEL])
OPTION_WITH([cray-hss-devel], [CRAY_HSS_DEVEL],[/usr])

# for aries_mmr
OPTION_ARIES_MMR

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[mmalloc third coll ovis_util zap ovis_ctrl ovis_event ovis_auth],[ovis-auth.sh],[ovis-lib],[ovis_auth])
dnl substitute values we need from ovis-auth.sh during joint configure
AC_SUBST(OVIS_AUTH_LIBS)

if test -d ${OVIS_LIB_LIB64DIR}/ovis-lib ; then
	ZAP_LIBPATH=${OVIS_LIB_LIB64DIR}/ovis-lib
else
	ZAP_LIBPATH=${OVIS_LIB_LIBDIR}/ovis-lib
fi
ZAP_LIBPATH=${ZAP_LIBPATH#*/rpm7/BUILDROOT}
AC_SUBST(ZAP_LIBPATH)
LDMSD_PLUGIN_LIBPATH=${pkglibdir}
AC_SUBST(LDMSD_PLUGIN_LIBPATH)

dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT],[/usr])

OPTION_DEFAULT_DISABLE([ocm], [ENABLE_OCM])
OPTION_WITH_OR_BUILD([ocm], [../ocm/src],[ocm ovis_rctrl],[ocm-vars.sh],[ocm],[])

OPTION_DEFAULT_DISABLE([ldmsd-rctl], [ENABLE_LDMSD_RCTL])
OPTION_DEFAULT_ENABLE([ldms-python], [ENABLE_LDMS_PYTHON])
OPTION_DEFAULT_DISABLE([ldms-test], [ENABLE_LDMS_TEST])

dnl Check for libevent
OPTION_WITH_EVENT

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

if test "x$WITH_OVIS_LIB" != "xbuild" && test -z "$ENABLE_TSAMPLER_TRUE"; then
	dnl tsampler uses ovis_event (from lib/)
	OCFLAGS=$CFLAGS
	CFLAGS=$OVIS_LIB_INCDIR_FLAG
	AC_CHECK_HEADERS(ovis_event/ovis_event.h, [],
			 AC_MSG_FAILURE([ovis_event/ovis_event.h not found.])
			 )
	AC_CHECK_LIB(ovis_event, ovis_event_create, [],
		     AC_MSG_ERROR([ovis_event library not found.]),
		     [ $OVIS_LIB_LIB64DIR_FLAG  $OVIS_LIB_LIBDIR_FLAG ]
		    )
	CFLAGS=$OCFLAGS
	LIBS=""
fi

if test -z "$ENABLE_POW_TRUE"; then
	OCFLAGS=$CFLAGS
	CFLAGS=-I$POW_INCDIR
	AC_CHECK_HEADERS(pwr.h,
			 [],
			 AC_MSG_FAILURE([pow.h not found])
			 )
	AC_CHECK_LIB(pwr, PWR_CntxtInit, [],
		     AC_MSG_ERROR([PWR_CntxtInit() function not found. powerapi
				   sampler requires libpow.]),
		     [ $POW_LIB64DIR_FLAG $POW_LIBDIR_FLAG ]
		)
	CFLAGS=$OCFLAGS
	LIBS=""
fi


if test -z "$ENABLE_SOS_TRUE"; then
	if test "x$WITH_SOS" != "xbuild"; then
		OCFLAGS=$CFLAGS
		CFLAGS=-I$SOS_INCDIR
		AC_CHECK_HEADERS(sos/sos.h,
			[],
			AC_MSG_FAILURE([sos/sos.h not found])
		)
		AC_CHECK_LIB( sos, sos_container_open,
			[],
			AC_MSG_FAILURE(libsos not found),
			[ $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG ]
		)
		CFLAGS=$OCFLAGS
		LIBS=""
	else
		AC_MSG_NOTICE([Using staged ovis-sos $OVIS_SOS_LIB64DIR_FLAG $OVIS_SOS_LIBDIR_FLAG])
	fi
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

dnl Check for mmalloc
if test "x$WITH_OVIS_LIB" != "xbuild"; then
	AC_CHECK_LIB(mmalloc, mm_init, [],
		AC_MSG_ERROR([libmmalloc not found. Please install ovis-lib.]),
		$OVIS_LIB_LIB64DIR_FLAG  $OVIS_LIB_LIBDIR_FLAG )
	dnl Reset LIBS variable.
	LIBS=""


	AC_CHECK_LIB(coll, idx_create, [],
		AC_MSG_ERROR([libcoll not found. Please install ovis-lib.]),
		$OVIS_LIB_LIB64DIR_FLAG $OVIS_LIB_LIBDIR_FLAG)
	dnl Reset LIBS variable.
	LIBS=""
else
	AC_MSG_NOTICE([Using staged ovis-lib $OVIS_LIB_LIB64DIR_FLAG $OVIS_LIB_LIBDIR_FLAG for LDMS.])
fi

dnl LDMS Swig
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"; then
	AC_PROG_SWIG
	SWIG_PYTHON
fi

if test -z "$ENABLE_SWIG_TRUE" || test -z "$ENABLE_LDMS_PYTHON_TRUE"
then
	AM_PATH_PYTHON(2.6)
	pkgpythondir="${pythondir}/ovis_ldms"
	pkgpyexecdir="${pkgpythondir}"
	AX_PYTHON_MODULE_VERSION(["argparse"], 1.1, "python")
fi

OPTION_WITH_PORT([LDMSD],[411])

OPTION_WITH_MAGIC([LDMS_MSG_MAX],[32768],[Maximum control message length allowed to ldmsd (min 4095)])

dnl if cray_sampler set some other variables
if test "x$enable_cray_system_sampler" = "xyes"
then

	if test -z "$ENABLE_SPACELESS_NAMES_TRUE"; then
	   AC_DEFINE([HAVE_SPACELESS_NAMES],[1],[cray_system_sampler: Using spaceless names.])
	fi

	if !(test -z "$ENABLE_GEMINI_GPCDR_TRUE") && !(test -z "$ENABLE_ARIES_GPCDR_TRUE")
	then
		AC_MSG_ERROR([Must enable at least one of gemini-gpcdr, and aries-gpcdr.])
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

dnl if knc, check for libmicmgmt
if test "x$enable_knc_sampler" = "xyes"
then

	AC_CHECK_HEADERS(miclib.h, [],
	AC_MSG_FAILURE([ not found])
	)

        AC_CHECK_LIB(micmgmt, mic_open_device, [],
            AC_MSG_FAILURE([micmgmt library not found.  knc_sampler requires libmicmgmt.]))
fi

dnl Reset LIBS variable.
LIBS=""


OPTION_GITINFO
OPTION_HOSTINFO

# define substitutions for configvars and other sed-generated files.
# note carefully the escapes.
OVIS_DO_SUBST([LDMS_SUBST_RULE], ["sed \
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
-e 's,[[@]]pythondir[[@]],\$(pythondir),g' \
"
])

AC_SUBST(ac_configure_args)
AC_SUBST(configure_input)

dnl handle test generation and chmod
OVIS_EXEC_SCRIPTS(
		 scripts/ldms-wrapper
		 scripts/ldms-static-test.sh
		 scripts/pll-ldms-static-test.sh
		 scripts/ldms_ban.sh
		 scripts/ldms_plugins_list.sh
		 scripts/ldms-py-subset_test.sh
		 scripts/ldms-py-syslog.sh
		 scripts/ldms-py-rename.sh
		 scripts/ldms-meminfo.sh
)

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
		 swig/Makefile
		 src/store/Makefile src/sampler/Makefile
		 src/sampler/dstat/Makefile
		 src/sampler/filesingle/Makefile
		 src/sampler/lustre/Makefile
		 src/sampler/llnl/Makefile
		 src/sampler/hadoop/Makefile
		 src/sampler/hadoop/hadoop-sinks/OvisSink.java
		 src/sampler/cray_system_sampler/Makefile
		 src/sampler/aries_mmr/Makefile
		 src/ldmsd/Makefile
		 scripts/Makefile
		 src/test/Makefile
		 etc/Makefile
		 etc/systemd/Makefile
		 python/Makefile
		 python/ldmsd/Makefile
		 Doxyfile
		 scripts/ldmsd
		 scripts/envldms.sh
		 ldms.spec
		 man/Makefile
])

AC_CONFIG_COMMANDS([scripts],[ echo fixing test script perms.
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
