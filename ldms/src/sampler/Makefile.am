include $(top_srcdir)/ldms/rules.mk


ACLOCAL_AMFLAGS = -I m4
# unlike default automake, we build this dir's local pieces before its children.
SUBDIRS = .
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
noinst_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

CORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	      @LDFLAGS_GETTIME@ \
	      $(top_builddir)/lib/src/ovis_util/libovis_util.la
COMMON_LIBADD = libsampler_base.la $(CORE_LIBADD) \
		$(top_builddir)/lib/src/coll/libcoll.la

noinst_LTLIBRARIES += libldms_compid_helper.la
libldms_compid_helper_la_SOURCES = comp_id_helper.c comp_id_helper.h
libldms_compid_helper_la_LIBADD = $(CORE_LIBADD)

noinst_LTLIBRARIES += libjobid_helper.la
libjobid_helper_la_SOURCES = jobid_helper.c jobid_helper.h
libjobid_helper_la_LIBADD = $(CORE_LIBADD) $(top_builddir)/lib/src/coll/libcoll.la

libsampler_base_la_SOURCES = sampler_base.c sampler_base.h
libsampler_base_la_LIBADD = $(CORE_LIBADD)
lib_LTLIBRARIES += libsampler_base.la

ldmssamplerincludedir = $(includedir)/ldms/sampler
ldmssamplerinclude_HEADERS = sampler_base.h

SUBDIRS += netlink
SUBDIRS += json

dist_man7_MANS += ldms_sampler_base.man

if ENABLE_LUSTRE
SUBDIRS += lustre_client
SUBDIRS += lustre_mdt
SUBDIRS += lustre_ost
SUBDIRS += lustre_mdc
endif

if HAVE_DCGM
SUBDIRS += dcgm_sampler
endif

if ENABLE_IBNET
if HAVE_LIBIBMAD
if HAVE_LIBIBUMAD
if HAVE_LIBOSMCOMP
if HAVE_COMPLIB_CL_NODENAMEMAP_H
SUBDIRS += ibnet
endif
endif
endif
endif
endif

if ENABLE_RDC
SUBDIRS += rdc_sampler
endif

if ENABLE_JOBID
AM_CFLAGS = -DENABLE_JOBID
JOBID_LIBFLAGS = libjobid.la
libjobid_la_SOURCES = ldms_jobid.c
libjobid_la_LIBADD = $(COMMON_LIBADD)
ldmssamplerinclude_HEADERS += ldms_jobid.h
pkglib_LTLIBRARIES += libjobid.la
dist_man7_MANS += ldms-sampler_jobid.man
endif

if ENABLE_TSAMPLER
SUBDIRS += tsampler
endif

if ENABLE_KGNILND
SUBDIRS += kgnilnd
endif

if ENABLE_VMSTAT
SUBDIRS += vmstat
endif
if ENABLE_LOADAVG
SUBDIRS += loadavg
endif

if ENABLE_CORETEMP
SUBDIRS += coretemp
endif

if ENABLE_CLOCK
SUBDIRS += clock
endif

if ENABLE_FPTRANS
SUBDIRS += fptrans
endif

# always include examples
SUBDIRS += examples

if ENABLE_VARSET
SUBDIRS += variable
endif

if ENABLE_LLNL
SUBDIRS += llnl
endif

if ENABLE_JOBINFO
SUBDIRS += job_info
endif

if HAVE_SLURM
SUBDIRS += job_info_slurm
SUBDIRS += spank
endif

if ENABLE_SPANK_SUBSCRIBER
# This slurm sampler does not have a slurm build dependency
SUBDIRS += slurm
endif

if ENABLE_LUSTRE
SUBDIRS += lustre
endif

if ENABLE_IBM_OCC_SAMPLER
SUBDIRS += ibm_occ
endif

if ENABLE_CRAY_SYSTEM_SAMPLER
SUBDIRS += cray_system_sampler
endif

if ENABLE_ARIES_MMR
SUBDIRS += aries_mmr
endif

if ENABLE_LNET_STATS
SUBDIRS += lnet_stats
endif

if ENABLE_TX2MON
SUBDIRS += tx2mon
endif

if ENABLE_MEMINFO
SUBDIRS += meminfo
endif

if ENABLE_MSR_INTERLAGOS
SUBDIRS += msr_interlagos
endif

if ENABLE_SWITCHX
libswitchx_la_SOURCES = switchx.c
libswitchx_la_LIBADD = $(COMMON_LIBADD) \
	$(SWITCHX_LIBDIR)/libsxapi.a \
	$(SWITCHX_LIBDIR)/libsxutils.a \
	$(SWITCHX_LIBDIR)/libsxdemadparser.a \
	$(SWITCHX_LIBDIR)/libsxcomp.a \
	$(SWITCHX_LIBDIR)/libsxdev.a \
	$(SWITCHX_LIBDIR)/libcommon.a \
	$(SWITCHX_LIBDIR)/libsxlog.a \
	$(SWITCHX_LIBDIR)/libibmad.a \
	$(SWITCHX_LIBDIR)/libibumad.a \
	$(SWITCHX_LIBDIR)/libosmcomp.a
libswitchx_la_CFLAGS = $(AM_CFLAGS) \
	-I$(SWITCHX_LIBDIR)/../usr/include
pkglib_LTLIBRARIES += libswitchx.la
endif

if ENABLE_PERFEVENT
SUBDIRS += perfevent
endif

if ENABLE_APPINFO
libappinfo_la_SOURCES = appinfo.c
libappinfo_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/appinfo_lib
libappinfo_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libappinfo.la
SUBDIRS += appinfo_lib
endif

if ENABLE_MPI_SAMPLER
SUBDIRS += shm
endif

if ENABLE_PAPI
if HAVE_LIBPAPI
SUBDIRS += papi

if HAVE_LIBPFM
SUBDIRS += syspapi
SUBDIRS += rapl
SUBDIRS += hweventpapi
endif

endif
endif

if ENABLE_PROCDISKSTATS
SUBDIRS += procdiskstats
endif

if ENABLE_PROCINTERRUPTS
SUBDIRS += procinterrupts
endif

if ENABLE_PROCNETDEV
SUBDIRS += procnetdev
SUBDIRS += procnetdev2
endif

if ENABLE_PROCNFS
SUBDIRS += procnfs
endif

if ENABLE_FILESINGLE
SUBDIRS += filesingle
endif

if ENABLE_PROCNET
SUBDIRS += procnet
endif

if ENABLE_PROCSTAT
SUBDIRS += procstat
SUBDIRS += procstat2
endif

if ENABLE_DSTAT
SUBDIRS += dstat
endif

if ENABLE_INFINIBAND
if HAVE_LIBIBMAD
if HAVE_LIBIBUMAD
if HAVE_IBA_IB_TYPES_H
SUBDIRS += ibmad_sampler
SUBDIRS += ibmad_records_sampler
SUBDIRS += sysclassib
endif
endif
endif
endif

if ENABLE_OPA2
if HAVE_LIBIBMAD
if HAVE_LIBIBUMAD
SUBDIRS += opa2
endif
endif
endif

if ENABLE_ATASMART
SUBDIRS += sampler_atasmart
endif

if ENABLE_GENERIC_SAMPLER
libgeneric_sampler_la_SOURCES = generic_sampler.c
libgeneric_sampler_la_LIBADD = $(COMMON_LIBADD) \
			       $(top_builddir)/lib/src/coll/libcoll.la
pkglib_LTLIBRARIES += libgeneric_sampler.la
endif

if ENABLE_GRPTEST_LDMS_TEST
libgrptest_la_SOURCES = grptest.c
libgrptest_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libgrptest.la
endif

if ENABLE_APP_SAMPLER
SUBDIRS += app_sampler
endif

if ENABLE_HELLO_STREAM
SUBDIRS += hello_stream
endif

if ENABLE_BLOB_STREAM
SUBDIRS += blob_stream
endif

if ENABLE_ZFS
SUBDIRS += zfs_topvdevs
SUBDIRS += zfs_leafvdevs
SUBDIRS += zfs_zpool
endif

if ENABLE_SLINGSHOT
if HAVE_LIBCXI
SUBDIRS += slingshot_metrics
SUBDIRS += slingshot_info
endif
endif

CLEANFILES = $(dist_man7_MANS)
