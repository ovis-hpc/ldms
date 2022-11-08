ACLOCAL_AMFLAGS = -I m4
# unlike default automake, we build this dir's local pieces before its children.
SUBDIRS = .
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
noinst_LTLIBRARIES =

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

if HAVE_NETLINK
SUBDIRS += netlink
endif
SUBDIRS += lustre_client
SUBDIRS += lustre_mdt
SUBDIRS += lustre_ost
SUBDIRS += lustre_mdc

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
endif

if ENABLE_TSAMPLER
libtsampler_la_SOURCES = tsampler.c tsampler.h
libtsampler_la_LIBADD = $(COMMON_LIBADD) \
			$(top_builddir)/lib/src/ovis_event/libovis_event.la -lpthread
pkglib_LTLIBRARIES += libtsampler.la

libtimer_base_la_SOURCES = timer_base.c timer_base.h
libtimer_base_la_LIBADD = $(COMMON_LIBADD) libtsampler.la $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libtimer_base.la

libhfclock_la_SOURCES = hfclock.c
libhfclock_la_LIBADD = $(COMMON_LIBADD) libtimer_base.la
pkglib_LTLIBRARIES += libhfclock.la

if ENABLE_CRAY_POWER_SAMPLER
libcray_power_sampler_la_SOURCES = cray_power_sampler.c
libcray_power_sampler_la_CFLAGS = $(AM_CFLAGS)
libcray_power_sampler_la_LIBADD = $(COMMON_LIBADD) \
			     libtimer_base.la
pkglib_LTLIBRARIES += libcray_power_sampler.la
endif

endif #ENABLE_TSAMPLER

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
libclock_la_SOURCES = clock.c
libclock_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libclock.la
endif

if ENABLE_FPTRANS
libfptrans_la_SOURCES = fptrans.c
libfptrans_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libfptrans.la
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

# This slurm sampler does not have a slurm build dependency
SUBDIRS += slurm

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
liblnet_stats_la_SOURCES = lnet_stats.c
liblnet_stats_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liblnet_stats.la
endif

if ENABLE_TX2MON
SUBDIRS += tx2mon
endif

if ENABLE_MEMINFO
SUBDIRS += meminfo
endif

if ENABLE_MSR_INTERLAGOS
libmsr_interlagos_la_SOURCES = msr_interlagos.c
libmsr_interlagos_la_LIBADD = $(COMMON_LIBADD) -lpthread
pkglib_LTLIBRARIES += libmsr_interlagos.la
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
libperfevent_la_SOURCES = perfevent.c
libperfevent_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libperfevent.la
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

if HAVE_LIBPAPI
SUBDIRS += papi
if HAVE_LIBPFM
SUBDIRS += syspapi

libhweventpapi_la_SOURCES = hweventpapi.c
libhweventpapi_la_CFLAGS = $(AM_CFLAGS)
libhweventpapi_la_LDFLAGS = $(AM_LDFLAGS)
libhweventpapi_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS) $(LTLIBPAPI) $(LTLIBPFM) -lm
pkglib_LTLIBRARIES += libhweventpapi.la

librapl_la_SOURCES = rapl.c
librapl_la_CFLAGS = $(AM_CFLAGS)
librapl_la_LDFLAGS = $(AM_LDFLAGS) $(PAPI_LDFLAGS)
librapl_la_LIBADD = $(COMMON_LIBADD) $(LTLIBPAPI) $(LTLIBPFM) -lm
pkglib_LTLIBRARIES += librapl.la
endif
endif

if ENABLE_PROCDISKSTATS
libprocdiskstats_la_SOURCES = procdiskstats.c
libprocdiskstats_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocdiskstats.la
endif

if ENABLE_PROCINTERRUPTS
SUBDIRS += procinterrupts
endif

if ENABLE_PROCNETDEV
libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnetdev.la
SUBDIRS += procnetdev2
endif

if ENABLE_PROCNFS
libprocnfs_la_SOURCES = procnfs.c
libprocnfs_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnfs.la
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
endif
libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD) $(LTLIBIBMAD) $(LTLIBIBUMAD)
pkglib_LTLIBRARIES += libsysclassib.la
endif
endif
endif

if ENABLE_OPA2
if HAVE_LIBIBMAD
if HAVE_LIBIBUMAD
OPA2_LIBADD = $(LTLIBIBMAD) $(LTLIBIBUMAD) -lpthread
libopa2_la_SOURCES = opa2.c
libopa2_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
libopa2_la_LIBADD = $(AM_LDFLAGS) $(COMMON_LIBADD) $(OPA2_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libopa2.la
endif
endif
endif

if ENABLE_ATASMART
libsampler_atasmart_la_SOURCES = sampler_atasmart.c
libsampler_atasmart_la_LIBADD = $(COMMON_LIBADD) -latasmart
pkglib_LTLIBRARIES += libsampler_atasmart.la
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

if ENABLE_SLINGSHOT
if HAVE_LIBCXI
SUBDIRS += slingshot_metrics
SUBDIRS += slingshot_info
endif
endif
