dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(ovis-ldms, 2.1.1, ovis-help@sandia.gov)
AC_CONFIG_MACRO_DIR([m4])
dnl AC_CONFIG_SRCDIR([src/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
dnl AM _INIT_AUTOMAKE([-Wno-portability])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_LIBTOOL

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

OPTION_DEFAULT_DISABLE([doc], [ENABLE_DOC])
OPTION_DEFAULT_DISABLE([etc], [ENABLE_ETC])

OPTION_DEFAULT_ENABLE([core], [ENABLE_CORE])
OPTION_DEFAULT_ENABLE([papi], [ENABLE_PAPI])
OPTION_DEFAULT_ENABLE([mmap], [ENABLE_MMAP])
OPTION_DEFAULT_ENABLE([perf], [ENABLE_PERF])
OPTION_DEFAULT_ENABLE([sensors], [ENABLE_SENSORS])
OPTION_DEFAULT_ENABLE([glib], [ENABLE_GLIB])
OPTION_DEFAULT_DISABLE([yaml], [ENABLE_YAML])
OPTION_DEFAULT_DISABLE([auth], [ENABLE_AUTH])

dnl Options for store
OPTION_DEFAULT_ENABLE([store], [ENABLE_STORE])
OPTION_DEFAULT_DISABLE([mysql], [ENABLE_MYSQL])
OPTION_DEFAULT_DISABLE([mysqlbulk], [ENABLE_MYSQLBULK])
OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])

OPTION_DEFAULT_DISABLE([tahoma_sos], [ENABLE_TAHOMA_SOS])
OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_WITH([sos], [SOS])

dnl Options for consumer
OPTION_DEFAULT_DISABLE([me], [ENABLE_ME])
OPTION_WITH([me], [ME])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_DISABLE([geminfo], [ENABLE_GEMINFO])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_DISABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([procsensors], [ENABLE_PROCSENSORS])
OPTION_DEFAULT_ENABLE([procstatutil], [ENABLE_PROCSTATUTIL])
OPTION_DEFAULT_DISABLE([sedc], [ENABLE_SEDC])
OPTION_DEFAULT_DISABLE([sysclassib], [ENABLE_SYSCLASSIB])
OPTION_DEFAULT_ENABLE([vmstat], [ENABLE_VMSTAT])
OPTION_DEFAULT_ENABLE([procdiskstats], [ENABLE_PROCDISKSTATS])
OPTION_DEFAULT_DISABLE([ncsa_unified], [ENABLE_NCSA_UNIFIED])
OPTION_DEFAULT_DISABLE([atasmart], [ENABLE_ATASMART])
OPTION_DEFAULT_DISABLE([hadoop], [ENABLE_HADOOP])
OPTION_DEFAULT_ENABLE([generic_sampler], [ENABLE_GENERIC_SAMPLER])
OPTION_DEFAULT_DISABLE([switchx], [ENABLE_SWITCHX])
OPTION_WITH([switchx], [SWITCHX])

OPTION_DEFAULT_ENABLE([ldmsd], [ENABLE_LDMSD])

dnl Other libraries

OPTION_DEFAULT_DISABLE([gpcd], [ENABLE_GPCD])
OPTION_WITH([gpcd], [GPCD])
OPTION_DEFAULT_DISABLE([rca], [ENABLE_RCA])
OPTION_WITH([rca], [RCA])
OPTION_DEFAULT_DISABLE([krca], [ENABLE_KRCA])
OPTION_WITH([krca], [KRCA])
OPTION_DEFAULT_DISABLE([cray-hss-devel], [ENABLE_CRAY_HSS_DEVEL])
OPTION_WITH([cray-hss-devel], [CRAY_HSS_DEVEL])

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH([ovis-lib], [OVIS_LIB])

dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT])

OPTION_DEFAULT_DISABLE([ocm], [ENABLE_OCM])
OPTION_WITH([ocm], [OCM])

OPTION_DEFAULT_DISABLE([ldmsd-rctl], [ENABLE_LDMSD_RCTL])
OPTION_DEFAULT_DISABLE([ldms-python], [ENABLE_LDMS_PYTHON])

if test -z "$ENABLE_MYSQL_TRUE"
then
OPTION_WITH_MYSQL dnl MYSQL is special.
fi
if test -z "$ENABLE_GLIB_TRUE"
then
PKG_CHECK_MODULES([GLIB20], [glib-2.0])
fi

dnl Check for libevent
OPTION_WITH_EVENT

dnl Check for pthread support
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
dnl Reset LIBS variable.
LIBS=""

dnl Check for libreadline
AX_LIB_READLINE
READLINE_LDFLAGS=$READLINE_LIBS
AC_SUBST([READLINE_LDFLAGS])
AC_SUBST([READLINE_CFLAGS])
LIBS=""

dnl Checks for libraries
if test -z "$ENABLE_SYSCLASSIB_TRUE"
then
AC_CHECK_LIB(ibmad, mad_rpc_open_port, [],
    AC_MSG_ERROR([mad_rpc_open_port() function not found. sysclassib sampler
		  requires libibmad.]))
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

if test -z "$ENABLE_SOS_TRUE"
then
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
dnl Reset LIBS variable.
CFLAGS=$OCFLAGS
LIBS=""
fi

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
AC_CHECK_LIB(mmalloc, mm_init, [],
	     AC_MSG_ERROR([mmalloc library not found. ldms requires mmalloc.
			   Please install mmalloc from OVIS/lib. ]),
	     $OVIS_LIB_LIBDIR_FLAG )
dnl Reset LIBS variable.
LIBS=""

if test -z "$ENABLE_AUTH"
then
dnl Check for OpenSSL
AX_CHECK_OPENSSL
fi

AC_CHECK_LIB(coll, idx_create, [],
	     AC_MSG_ERROR([libcoll not found. Please install libcoll from
			   OVIS/lib.]),
	     $OVIS_LIB_LIBDIR_FLAG )
dnl Reset LIBS variable.
LIBS=""

if test -z "$ENABLE_SWIG_TRUE" || test -z "$ENABLE_LDMS_PYTHON_TRUE"
then
	AM_PATH_PYTHON(2.6)
	pkgpythondir="${pythondir}/ovis_ldms"
	pkgpyexecdir="${pkgpythondir}"
fi

dnl LDMS Swig
OPTION_DEFAULT_DISABLE([swig], [ENABLE_SWIG])
if test -z "$ENABLE_SWIG_TRUE"
then
	AC_PROG_SWIG
	SWIG_PYTHON
fi

AC_SUBST(ac_configure_args)
AC_SUBST(configure_input)

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile
		 swig/Makefile
		 src/store/Makefile src/sampler/Makefile
		 src/sampler/lustre/Makefile
		 src/sampler/hadoop/Makefile
		 src/ldmsd/Makefile
		 etc/Makefile
		 etc/init.d/Makefile
		 etc/init.d/ldmsd_aggregator
		 etc/init.d/ldmsd_sampler
		 etc/logrotate.d/Makefile
		 python/Makefile
		 python/ldmsd/Makefile
		 packaging/ovis-ldms.spec
		 Doxyfile
		 ldms.spec])

AC_OUTPUT
