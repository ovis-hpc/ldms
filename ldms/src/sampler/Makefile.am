ACLOCAL_AMFLAGS = -I m4
# unlike default automake, we build this dir's local pieces before its children.
SUBDIRS = .
pkglib_LTLIBRARIES =
CORE = ../core

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util
ldmssamplerincludedir = $(includedir)/ldms/sampler
ldmssamplerinclude_HEADERS =
BASE_SRC=sampler_base.c sampler_base.h

if ENABLE_JOBID
AM_CFLAGS += -DENABLE_JOBID
JOBID_LIBFLAGS = libjobid.la
libjobid_la_SOURCES = ldms_jobid.c
libjobid_la_LIBADD = $(COMMON_LIBADD)
ldmssamplerinclude_HEADERS += ldms_jobid.h
pkglib_LTLIBRARIES += libjobid.la
endif

if ENABLE_TSAMPLER
libtsampler_la_SOURCES = tsampler.c tsampler.h
libtsampler_la_LIBADD = $(COMMON_LIBADD) -lovis_event -lpthread
pkglib_LTLIBRARIES += libtsampler.la

libtimer_base_la_SOURCES = timer_base.c timer_base.h  $(BASE_SRC)
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

if ENABLE_POW
libpower_sampler_la_SOURCES = power_sampler.c
libpower_sampler_la_CFLAGS = $(AM_CFLAGS) @POW_INCDIR_FLAG@
libpower_sampler_la_LIBADD = $(COMMON_LIBADD) \
			     -lpwr @POW_LIBDIR_FLAG@ @POW_LIB64DIR_FLAG@ \
			     libtimer_base.la
pkglib_LTLIBRARIES += libpower_sampler.la
endif

endif #ENABLE_TSAMPLER

if ENABLE_KGNILND
libkgnilnd_la_SOURCES = kgnilnd.c
libkgnilnd_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libkgnilnd.la
endif

if ENABLE_CLOCK
libclock_la_SOURCES = clock.c
libclock_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libclock.la
endif

if ENABLE_FPTRANS
libfptrans_la_SOURCES = fptrans.c
libfptrans_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libfptrans.la
endif

if ENABLE_SYNTHETIC
libsynthetic_la_SOURCES = synthetic.c
libsynthetic_la_LIBADD = $(COMMON_LIBADD) -lm $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libsynthetic.la
endif

if ENABLE_VARSET
libvariable_la_SOURCES = variable.c
libvariable_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libvariable.la
endif

if ENABLE_LLNL
SUBDIRS += llnl
endif

if ENABLE_JOBINFO
SUBDIRS += job_info
endif

if ENABLE_SLURM_JOBINFO
SUBDIRS += job_info_slurm
endif

if ENABLE_LUSTRE
SUBDIRS += lustre
endif

if ENABLE_HADOOP
SUBDIRS += hadoop
endif

if ENABLE_CRAY_SYSTEM_SAMPLER
SUBDIRS += cray_system_sampler
endif

if ENABLE_ARIES_MMR
SUBDIRS += aries_mmr
endif

if ENABLE_ARRAY_EXAMPLE
libarray_example_la_SOURCES = array_example.c
libarray_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libarray_example.la

liball_example_la_SOURCES = all_example.c
liball_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liball_example.la
endif

if ENABLE_KNC_SAMPLER
libknc_sampler_la_SOURCES = knc_sampler.c
libknc_sampler_la_LIBADD = $(COMMON_LIBADD) -lmicmgmt
pkglib_LTLIBRARIES += libknc_sampler.la

libknc_sampler_copy_la_SOURCES = knc_sampler_copy.c
libknc_sampler_copy_la_LIBADD = $(COMMON_LIBADD) -lmicmgmt
pkglib_LTLIBRARIES += libknc_sampler_copy.la

libknc_sampler_derived_la_SOURCES = knc_sampler_derived.c
libknc_sampler_derived_la_LIBADD = $(COMMON_LIBADD) -lmicmgmt
pkglib_LTLIBRARIES += libknc_sampler_derived.la
endif

if ENABLE_LNET_STATS
liblnet_stats_la_SOURCES = lnet_stats.c
liblnet_stats_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += liblnet_stats.la
endif

if ENABLE_MEMINFO
libmeminfo_la_SOURCES = meminfo.c $(BASE_SRC)
libmeminfo_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libmeminfo.la
endif

if ENABLE_MSR_INTERLAGOS
libmsr_interlagos_la_SOURCES = msr_interlagos.c
libmsr_interlagos_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS) -lpthread
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
libperfevent_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS) -lm
pkglib_LTLIBRARIES += libperfevent.la
endif

if ENABLE_HWEVENTPAPI
libhweventpapi_la_SOURCES = hweventpapi.c
libhweventpapi_la_CFLAGS = $(AM_CFLAGS) $(LIBPAPI_INCDIR_FLAG) $(LIBPAPI_LIBDIR_FLAG)
libhweventpapi_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS) -lpapi -lm
pkglib_LTLIBRARIES += libhweventpapi.la
endif

if ENABLE_MPI_SAMPLER
SUBDIRS += shm
endif

if ENABLE_RAPL
librapl_la_SOURCES = rapl.c
librapl_la_CFLAGS = $(AM_CFLAGS) $(LIBPAPI_INCDIR_FLAG)
librapl_la_LIBADD = $(COMMON_LIBADD) -lpapi -lm
pkglib_LTLIBRARIES += librapl.la
endif

if ENABLE_PROCDISKSTATS
libprocdiskstats_la_SOURCES = procdiskstats.c
libprocdiskstats_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libprocdiskstats.la
endif

if ENABLE_PROCINTERRUPTS
libprocinterrupts_la_SOURCES = procinterrupts.c
libprocinterrupts_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libprocinterrupts.la
endif

if ENABLE_PROCNETDEV
libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libprocnetdev.la
endif

if ENABLE_PROCNFS
libprocnfs_la_SOURCES = procnfs.c
libprocnfs_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libprocnfs.la
endif

if ENABLE_PROCSENSORS
libprocsensors_la_SOURCES = procsensors.c
libprocsensors_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libprocsensors.la
endif

if ENABLE_PROCSTAT
libprocstat_la_SOURCES = procstat.c
libprocstat_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libprocstat_la_CFLAGS = -I$(builddir) $(AM_CFLAGS)
pkglib_LTLIBRARIES += libprocstat.la
endif


if ENABLE_SYSCLASSIB
libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD) -libmad -libumad $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libsysclassib.la
endif

if ENABLE_OPA2
OPA2_LIBADD = -libmad -libumad -lpthread
libopa2_la_SOURCES = opa2.c $(BASE_SRC)
OPAINC=/usr/include/infiniband
libopa2_la_CFLAGS = $(AM_CFLAGS) -I$(OPAINC)
libopa2_la_LIBADD = $(AM_LDFLAGS) $(COMMON_LIBADD) $(OPA2_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libopa2.la
endif

if ENABLE_VMSTAT
libvmstat_la_SOURCES = vmstat.c $(BASE_SRC)
libvmstat_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libvmstat.la
endif

if ENABLE_ATASMART
libsampler_atasmart_la_SOURCES = sampler_atasmart.c
libsampler_atasmart_la_LIBADD = $(COMMON_LIBADD) -latasmart $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libsampler_atasmart.la
endif

if ENABLE_GENERIC_SAMPLER
libgeneric_sampler_la_SOURCES = generic_sampler.c
libgeneric_sampler_la_LIBADD = $(COMMON_LIBADD) -lcoll
pkglib_LTLIBRARIES += libgeneric_sampler.la
endif

if ENABLE_LDMS_TEST
libtest_sampler_la_SOURCES = test_sampler.c
libtest_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libtest_sampler.la
endif

