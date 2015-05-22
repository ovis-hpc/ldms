dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis-ldms, 2.4.1, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
dnl AC_CONFIG_SRCDIR([src/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
dnl AM _INIT_AUTOMAKE([-Wno-portability])
AM_INIT_AUTOMAKE([foreign])
m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes])
AC_PROG_LIBTOOL
AC_LIB_RPATH

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long double)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_FLEXIBLE_ARRAY_MEMBER
AX_CHECK_OPENSSL
OVIS_PKGLIBDIR

OPTION_DEFAULT_ENABLE([core], [ENABLE_CORE])
OPTION_DEFAULT_ENABLE([papi], [ENABLE_PAPI])
OPTION_DEFAULT_DISABLE([mmap], [ENABLE_MMAP])
OPTION_DEFAULT_ENABLE([perf], [ENABLE_PERF])
OPTION_DEFAULT_ENABLE([sensors], [ENABLE_SENSORS])
OPTION_DEFAULT_ENABLE([glib], [ENABLE_GLIB])
OPTION_DEFAULT_DISABLE([yaml], [ENABLE_YAML])
OPTION_DEFAULT_DISABLE([authentication], [ENABLE_AUTHENTICATION],[require shared secret to query ldmsd])
OPTION_DEFAULT_DISABLE([authdebug], [ENABLE_AUTHDEBUG],[log authentication steps])
OPTION_DEFAULT_DISABLE([use-tf], [ENABLE_USE_TF],[log tracing of calls])
OPTION_DEFAULT_DISABLE([use-rtc], [ENABLE_USE_RTC],[log rtc stamp with traced calls])
OPTION_DEFAULT_DISABLE([use-tid], [ENABLE_USE_TID],[log thread id with traced calls])
OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC],[generate doxygen output])

dnl Options for transport
OPTION_DEFAULT_ENABLE([xprt], [ENABLE_XPRT])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])

dnl Options for store
OPTION_DEFAULT_ENABLE([store], [ENABLE_STORE])
dnl OPTION_DEFAULT_DISABLE([mysql], [ENABLE_MYSQL])
OPTION_DEFAULT_DISABLE([mysqlbulk], [ENABLE_MYSQLBULK])
OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_ENABLE([kgnilnd], [ENABLE_KGNILND])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_ENABLE([slurmjobid], [ENABLE_SLURMJOBID])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_ENABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([procsensors], [ENABLE_PROCSENSORS])
OPTION_DEFAULT_ENABLE([procstatutil], [ENABLE_PROCSTATUTIL])
OPTION_DEFAULT_ENABLE([procstatutil2], [ENABLE_PROCSTATUTIL2])
dnl OPTION_DEFAULT_DISABLE([sedc], [ENABLE_SEDC])
OPTION_DEFAULT_DISABLE([sysclassib], [ENABLE_SYSCLASSIB])
OPTION_DEFAULT_ENABLE([vmstat], [ENABLE_VMSTAT])
OPTION_DEFAULT_ENABLE([procdiskstats], [ENABLE_PROCDISKSTATS])
OPTION_DEFAULT_DISABLE([cray_system_sampler], [ENABLE_CRAY_SYSTEM_SAMPLER])
OPTION_DEFAULT_DISABLE([atasmart], [ENABLE_ATASMART])
OPTION_DEFAULT_DISABLE([hadoop], [ENABLE_HADOOP])

OPTION_DEFAULT_ENABLE([ldmsd], [ENABLE_LDMSD])

dnl Other libraries
OPTION_DEFAULT_DISABLE([gemini-gpcdr], [ENABLE_GEMINI_GPCDR]) dnl use gpcdr for gemini (not mutually exclusive)
OPTION_DEFAULT_DISABLE([aries-gpcdr], [ENABLE_ARIES_GPCDR]) dnl use gpcdr for aries (not mutually exclusive)
OPTION_DEFAULT_DISABLE([gemini-gpcd], [ENABLE_GEMINI_GPCD]) dnl use gpcd for gemini (not mutually exclusive) for the path to gpcd if relevant
OPTION_WITH([gemini-gpcd], [GEMINI_GPCD])
OPTION_DEFAULT_DISABLE([cray-nvidia], [ENABLE_CRAY_NVIDIA])
OPTION_DEFAULT_DISABLE([cray-nvidia-inc], [ENABLE_CRAY_NVIDIA_INC])
OPTION_WITH([cray-nvidia-inc], [CRAY_NVIDIA_INC]) dnl for the path to nvidia include
dnl Will need to have the NVIDIA library in the LD_LIBRARY_PATH
OPTION_DEFAULT_DISABLE([rca], [ENABLE_RCA])
OPTION_WITH([rca], [RCA])
OPTION_DEFAULT_DISABLE([krca], [ENABLE_KRCA])
OPTION_WITH([krca], [KRCA])
OPTION_DEFAULT_DISABLE([cray-hss-devel], [ENABLE_CRAY_HSS_DEVEL])
OPTION_WITH([cray-hss-devel], [CRAY_HSS_DEVEL])


dnl Options for Testing
OPTION_DEFAULT_DISABLE([qc-sampler], [ENABLE_QC_SAMPLER]) dnl turn on QC testing of samplers


dnl OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
dnl if test "$enable_sos" == "yes"; then
dnl OPTION_WITH_OR_BUILD([sos],[../sos/src])
dnl fi

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
dnl OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[../lib/src/mmalloc ../lib/src/coll ../lib/src/ovis_util ../lib/src/zap ../lib/src/ovis_ctrl])
OPTION_WITH_OR_BUILD([ovis-lib],[../lib/src],[../lib/src/mmalloc ../lib/src/coll ../lib/src/ovis_util ../lib/src/ovis_ctrl])

dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT])

dnl if test -z "$ENABLE_MYSQL_TRUE"
dnl then
dnl OPTION_WITH_MYSQL dnl MYSQL is special.
dnl fi
dnl if test -z "$ENABLE_GLIB_TRUE"
dnl then
dnl PKG_CHECK_MODULES([GLIB20], [glib-2.0])
dnl fi

dnl Check for libevent
OPTION_WITH_EVENT

dnl Check for pthread support
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
dnl save LIBS variable.
TMPLIBS="$LIBS"

AX_LIB_READLINE

dnl Check for libreadline
dnl AC_ARG_WITH([ldflags-readline],[AS_HELP_STRING([--with-ldflags-readline],[link flags needed for deficient readline libraries, eg needing -ltermcap.])],[LDFLAGS_READLINE=$with_ldflags_readline],[])
dnl AC_MSG_NOTICE([User gave $LDFLAGS_READLINE for readline support])
dnl AC_SUBST([LDFLAGS_READLINE])
dnl AC_CHECK_LIB(readline, readline, [],
dnl     AC_MSG_ERROR([libreadline not found. ldms requires libreadline.]),[$LDFLAGS_READLINE])
dnl Reset LIBS variable.
dnl LIBS=""
LDFLAGS_READLINE=$LIBS
LIBS=$TMPLIBS
AC_SUBST([LDFLAGS_READLINE])

dnl Checks for libraries
if test -z "$ENABLE_SYSCLASSIB_TRUE"
then
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

dnl Checks for libraries
if test -z "$ENABLE_RDMA_TRUE"
then
AC_CHECK_LIB(ibverbs, ibv_get_device_list, [],
    AC_MSG_ERROR([ibv_get_device_list() not found.  ldms-utils requires libibverbs.]))
dnl Reset LIBS variable.
LIBS=""

dnl Checks for header files.
AC_CHECK_HEADER(infiniband/driver.h, [],
    AC_MSG_ERROR([<infiniband/driver.h> not found.  Is libibverbs installed?]))
AC_HEADER_STDC

dnl Checks for library functions
AC_CHECK_FUNCS(ibv_read_sysfs_file)

dnl Check for glib
PKG_CONFIG=`which pkg-config`
PKG_CHECK_MODULES([GLIB20], [glib-2.0 >= 2.0.0])

dnl Now check if for libibverbs device library extension
dummy=if$$
cat <<IBV_VERSION > $dummy.c
#include <infiniband/driver.h>
IBV_DEVICE_LIBRARY_EXTENSION
IBV_VERSION
IBV_DEVICE_LIBRARY_EXTENSION=`$CC $CPPFLAGS -E $dummy.c 2> /dev/null | tail -1`
rm -f $dummy.c
if test $IBV_DEVICE_LIBRARY_EXTENSION = IBV_DEVICE_LIBRARY_EXTENSION; then
    AC_MSG_ERROR([IBV_DEVICE_LIBRARY_EXTENSION not defined.  Is libibverbs new enough?])
fi
AC_SUBST(IBV_DEVICE_LIBRARY_EXTENSION)
fi

dnl if test -z "$ENABLE_SOS_TRUE"
dnl then
dnl 	if test "x$WITH_SOS" != "xbuild"; then
dnl 		AC_MSG_NOTICE([Trying SOS_INCDIR=$SOS_INCDIR and libs $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG ])
dnl 		AC_CHECK_LIB( sos, sos_open,
dnl 			[
dnl 				AC_CHECK_HEADERS($SOS_INCDIR/sos/sos.h,
dnl 				[],
dnl 				[
dnl 					AC_MSG_FAILURE([sos/sos.h not found])
dnl 				],
dnl 				)
dnl 			],
dnl 			[
dnl 				AC_MSG_FAILURE(libsos not found)
dnl 			],
dnl 			-lpthread -lods -loidx -lidx $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG
dnl 		)
dnl 		dnl Reset LIBS variable.
dnl 		LIBS=""
dnl 	else
dnl 		# assume libs will be there by the time we need and create a
dnl 		# fake include dir in the build tree. do no copying.
dnl 		SOS_INCDIR=`(cd ../sos/src && pwd)`/tmp
dnl 		SOS_INCDIR_FLAG=-I$SOS_INCDIR
dnl 		AC_MSG_NOTICE([Using staged SOS_INCDIR=$SOS_INCDIR and libs $SOS_LIB64DIR_FLAG $SOS_LIBDIR_FLAG ])
dnl 	fi
dnl fi

if test -z "$ENABLE_ATASMART_TRUE"
then
AC_CHECK_LIB(atasmart, sk_disk_smart_read_data, [],
	AC_MSG_ERROR([libatasmart not found. sampler_atasmart requires libatasmart and libatasmart-devel.]))
fi


if test -z "$ENABLE_YAML_TRUE"
then
AC_CHECK_LIB(yaml, yaml_parser_load, [],
	AC_MSG_ERROR([libyaml not found. please add --disable-yaml to disable yaml support.]))
dnl Reset LIBS variable.
LIBS=""
fi

dnl Check for mmalloc
if test "x$WITH_OVIS_LIB" != "xbuild"; then
AC_CHECK_LIB(mmalloc, mm_init, [],
	     AC_MSG_ERROR([mmalloc library not found. ldms requires mmalloc.
			   Please install mmalloc from OVIS/lib. ]),
	     $OVIS_LIB_LIB64DIR_FLAG  $OVIS_LIB_LIBDIR_FLAG )
dnl Reset LIBS variable.
LIBS=""

AC_CHECK_LIB(coll, idx_create, [],
	     AC_MSG_ERROR([libcoll not found. Please install libcoll from
			   OVIS/lib.]),
	     $OVIS_LIB_LIB64DIR_FLAG  $OVIS_LIB_LIBDIR_FLAG )
dnl Reset LIBS variable.
LIBS=""
else
   AC_MSG_NOTICE([Using staged ovis-lib $OVIS_LIB_LIB64DIR_FLAG  $OVIS_LIB_LIBDIR_FLAG for mmalloc/ and coll/. ])
fi


dnl if cray_sampler set some other variables
if test "x$enable_cray_system_sampler" = "xyes"
then

        if !(test -z "$ENABLE_GEMINI_GPCDR_TRUE") && !(test -z "$ENABLE_GEMINI_GPCD_TRUE") && !(test -z "$ENABLE_ARIES_GPCDR_TRUE")
        then
		AC_MSG_ERROR([Must enable at least one of gemini-gpcd, gemini-gpcdr, and aries-gpcdr.])
        fi

        if (test -z "$ENABLE_GEMINI_GPCDR_TRUE") || (test -z "$ENABLE_ARIES_GPCDR_TRUE")
        then
        gpcdrfile="/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
        if ! test -f $gpcdrfile; then
           AC_MSG_WARN([
         ++++++++++++++++++++++++++++++++++++++++++++
           gpcdr interface not available:
           $gpcdrfile
           We hope your runtime environment has it.
         ++++++++++++++++++++++++++++++++++++++++++++
          ])
          fi
          AC_MSG_RESULT([Building gpcdr interface for HSN metrics.])
        fi
        if test -z "$ENABLE_GEMINI_GPCD_TRUE"
	then
          AC_MSG_RESULT([Building gpcd interface for gemini metrics.])
        fi

	if test -z "$ENABLE_CRAY_NVIDIA_TRUE"
	then
	  AC_DEFINE([HAVE_CRAY_NVIDIA],[1],[cray_system_sampler: Including option for NVIDIA metrics.])
	else
          AC_MSG_RESULT([cray_system_sampler: Not including option for NVIDIA metrics.])
	fi

	if test -z "$ENABLE_LUSTRE_TRUE"
	then
		AC_DEFINE([HAVE_LUSTRE],[1],[cray_system_sampler: Include option for Lustre metrics.])
	else
		AC_MSG_RESULT([cray_system_sampler: Not including option for lustre metrics.])
	fi
fi

dnl Reset LIBS variable.
LIBS=""

if test -z "$ENABLE_QC_SAMPLER_TRUE"
then
AC_DEFINE([HAVE_QC_SAMPLER],[1],[Turning on QC tests for samplers.])
fi

if test -z "$ENABLE_AUTHENTICATION_TRUE"
then
AC_DEFINE([HAVE_AUTH],[1],[Turning on authentication checks for xprt connect.])
fi

if test -z "$ENABLE_AUTHDEBUG_TRUE"
then
AC_DEFINE([HAVE_AUTHDEBUG],[1],[Turning on auth debug logging.])
fi

if test -z "$ENABLE_USE_TF_TRUE"
then
AC_DEFINE([USE_TF],[1],[Turning on tracing facilities.])
else
AC_DEFINE([USE_TF],[0],[Turning on tracing facilities.])
fi

if test -z "$ENABLE_USE_RTC_TRUE"
then
AC_DEFINE([USE_RTC],[1],[Use rtc timestamp in trace.])
else
AC_DEFINE([USE_RTC],[0],[Use rtc timestamp in trace.])
fi

if test -z "$ENABLE_USE_TID_TRUE"
then
AC_DEFINE([USE_TID],[1],[Use thread id in trace.])
else
AC_DEFINE([USE_TID],[0],[Use thread id timestamp in trace.])
fi

OPTION_WITH_PORT([LDMSD],[411])

OPTION_WITH_MAGIC([LDMS_MSG_MAX],[32768],[Maximum control message length allowed to ldmsd (min 4095)])

OPTION_GITINFO
OPTION_HOSTINFO


AC_SUBST(ac_configure_args)
AC_SUBST(configure_input)

# test scripts to be generated and chmod before install go here.
# no longer have to chmod explicitly in list

OVIS_EXEC_SCRIPTS(
		 scripts/ldms_ctltest.sh
		 scripts/ldms_ban.sh
		 scripts/ldms_usertest.sh
		 scripts/ldms_local_usertest.sh
		 scripts/ldms_local_slurmtest.sh
		 scripts/ldms_local_valgrind.sh
		 scripts/ldms_local_clocktest.sh
		 scripts/ldms_local_syslog.sh
		 scripts/ldms_local_inspxe.sh
		 scripts/ldms_local_inspxe_rdma.sh
		 scripts/ldms_local_rdmatest.sh
		 scripts/ldms_local_rollbytes_test.sh
		 scripts/ldms_local_rollperiod_test.sh
		 scripts/ldms_local_rollrecords_test.sh
		 scripts/ldms_local_rolltime_test.sh
		 scripts/ldms_local_dertest.sh
		 scripts/ldms_local_dertest_big.sh
		 scripts/ldms_local_procstatutil.sh
		 scripts/ldms_local_csvorder.sh
)

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile src/xprt/Makefile
		 src/store/Makefile src/sampler/Makefile
		 src/sampler/lustre/Makefile
		 src/sampler/hadoop/Makefile
		 src/sampler/hadoop/hadoop-sinks/LdmsSink.java
		 src/sampler/cray_system_sampler/Makefile
		 src/ldmsd/Makefile
		 scripts/Makefile
		 packaging/ovis-ldms.spec
		 Doxyfile
		 scripts/ldmsd
		 scripts/envldms.sh
		 ldms.spec
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

