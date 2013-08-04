dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(ldms, 2.1.0, tom@ogc.us)
AC_CONFIG_MACRO_DIR([m4])
dnl AC_CONFIG_SRCDIR([src/ldms.h])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([-Wno-portability])
AM_PROG_LIBTOOL

OPTION_DEFAULT_ENABLE([core], [ENABLE_CORE])
OPTION_DEFAULT_ENABLE([papi], [ENABLE_PAPI])
OPTION_DEFAULT_ENABLE([mmap], [ENABLE_MMAP])
OPTION_DEFAULT_ENABLE([perf], [ENABLE_PERF])
OPTION_DEFAULT_ENABLE([sensors], [ENABLE_SENSORS])
OPTION_DEFAULT_ENABLE([glib], [ENABLE_GLIB])
OPTION_DEFAULT_DISABLE([yaml], [ENABLE_YAML])

dnl Options for transport
OPTION_DEFAULT_ENABLE([xprt], [ENABLE_XPRT])
OPTION_DEFAULT_DISABLE([ugni], [ENABLE_UGNI])
OPTION_DEFAULT_DISABLE([rdma], [ENABLE_RDMA])
OPTION_DEFAULT_ENABLE([sock], [ENABLE_SOCK])

dnl Options for store
OPTION_DEFAULT_ENABLE([store], [ENABLE_STORE])
OPTION_DEFAULT_DISABLE([sos], [ENABLE_SOS])
OPTION_DEFAULT_DISABLE([mysql], [ENABLE_MYSQL])
OPTION_DEFAULT_DISABLE([mysqlbulk], [ENABLE_MYSQLBULK])
OPTION_DEFAULT_ENABLE([flatfile], [ENABLE_FLATFILE])
OPTION_DEFAULT_ENABLE([csv], [ENABLE_CSV])

dnl Options for sampler
OPTION_DEFAULT_ENABLE([sampler], [ENABLE_SAMPLER])
OPTION_DEFAULT_ENABLE([geminfo], [ENABLE_GEMINFO])
OPTION_DEFAULT_ENABLE([lustre], [ENABLE_LUSTRE])
OPTION_DEFAULT_ENABLE([meminfo], [ENABLE_MEMINFO])
OPTION_DEFAULT_ENABLE([perfevent], [ENABLE_PERFEVENT])
OPTION_DEFAULT_ENABLE([procinterrupts], [ENABLE_PROCINTERRUPTS])
OPTION_DEFAULT_ENABLE([procnetdev], [ENABLE_PROCNETDEV])
OPTION_DEFAULT_ENABLE([procnfs], [ENABLE_PROCNFS])
OPTION_DEFAULT_ENABLE([procsensors], [ENABLE_PROCSENSORS])
OPTION_DEFAULT_ENABLE([procstatutil], [ENABLE_PROCSTATUTIL])
OPTION_DEFAULT_ENABLE([sedc], [ENABLE_SEDC])
OPTION_DEFAULT_ENABLE([sysclassib], [ENABLE_SYSCLASSIB])
OPTION_DEFAULT_ENABLE([vmstat], [ENABLE_VMSTAT])
OPTION_DEFAULT_ENABLE([procdiskstats], [ENABLE_PROCDISKSTATS])
OPTION_DEFAULT_DISABLE([gem_link_perf], [ENABLE_GEM_LINK_PERF])
OPTION_DEFAULT_DISABLE([atasmart], [ENABLE_ATASMART])

OPTION_DEFAULT_ENABLE([ldmsd], [ENABLE_LDMSD])

dnl Other libraries

OPTION_DEFAULT_DISABLE([gpcd], [ENABLE_GPCD])
OPTION_WITH([gpcd], [GPCD])
OPTION_DEFAULT_DISABLE([rca], [ENABLE_RCA])
OPTION_WITH([rca], [RCA])
OPTION_DEFAULT_DISABLE([kcra], [ENABLE_KRCA])
OPTION_WITH([kcra], [KCRA])

OPTION_WITH([sos], [SOS])

OPTION_DEFAULT_ENABLE([ovis-lib], [ENABLE_OVIS_LIB])
OPTION_WITH([ovis-lib], [OVIS_LIB])

dnl This got defined here for LIBEVENT_ENABLE
OPTION_DEFAULT_ENABLE([libevent], [ENABLE_LIBEVENT])
OPTION_WITH([libevent], [LIBEVENT])

if test -z "$ENABLE_MYSQL_TRUE"
then
OPTION_WITH_MYSQL dnl MYSQL is special.
fi
if test -z "$ENABLE_GLIB_TRUE"
then
PKG_CHECK_MODULES([GLIB20], [glib-2.0])
fi

dnl Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_SIZEOF(long)
dnl AM_PROG_CC_C_O

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Check for libevent
AC_CHECK_LIB(event, event_base_new, [],
    AC_MSG_ERROR([event_base_new() not found. ldms requires libevent.]))
dnl Reset LIBS variable.
LIBS=""

dnl Check for pthread support
AC_CHECK_LIB(pthread, pthread_mutex_init, [],
    AC_MSG_ERROR([pthread library not found.  ldms requires libpthread.]))
dnl Reset LIBS variable.
LIBS=""

dnl Check for libreadline
AC_CHECK_LIB(readline, readline, [],
    AC_MSG_ERROR([libreadline not found. ldms requires libreadline.]))
dnl Reset LIBS variable.
LIBS=""

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

if test -z "$ENABLE_SOS_TRUE"
then
AC_CHECK_LIB( sos, sos_open,
	[
		AC_CHECK_HEADERS($SOS_INCDIR/sos/sos.h,
		[],
		[
			AC_MSG_FAILURE([sos/sos.h not found])
		],
		)
	],
	[
		AC_MSG_FAILURE(libsos not found)
	],
	-lpthread -lods -loidx -lidx -L$SOS_LIBDIR
)
dnl Reset LIBS variable.
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
	     -L$OVIS_LIB_LIBDIR -L$SOS_LIBDIR )
dnl Reset LIBS variable.
LIBS=""

AC_CHECK_LIB(coll, idx_create, [],
	     AC_MSG_ERROR([libcoll not found. Please install libcoll from
			   OVIS/lib.]),
	     -L$OVIS_LIB_LIBDIR -L$SOS_LIBDIR )
dnl Reset LIBS variable.
LIBS=""

AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile src/Makefile src/core/Makefile src/xprt/Makefile
		 src/store/Makefile src/sampler/Makefile
		 src/sampler/lustre/Makefile
		 src/ldmsd/Makefile
		 ldms.spec])
AC_OUTPUT

