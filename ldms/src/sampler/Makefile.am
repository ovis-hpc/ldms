ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
pkglib_LTLIBRARIES =
CORE = ../core
BUILT_SOURCES =

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la -lrt -lovis_util

libclock_la_SOURCES = clock.c
libclock_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libclock.la

libsynthetic_la_SOURCES = synthetic.c
libsynthetic_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libsynthetic.la

if ENABLE_KGNILND
libkgnilnd_la_SOURCES = kgnilnd.c
libkgnilnd_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libkgnilnd.la
endif

if ENABLE_LUSTRE
SUBDIRS += lustre
endif

if ENABLE_CRAY_SYSTEM_SAMPLER
SUBDIRS += cray_system_sampler
endif

if ENABLE_HADOOP
SUBDIRS += hadoop
endif

if ENABLE_SLURMJOBID
libslurmjobid_la_SOURCES = slurmjobid.c
libslurmjobid_la_LIBADD = $(COMMON_LIBADD)
libslurmjobidincludedir = $(includedir)/ovis_util
libslurmjobidinclude_HEADERS = ldms_slurmjobid.h
pkglib_LTLIBRARIES += libslurmjobid.la
endif

if ENABLE_MEMINFO
libmeminfo_la_SOURCES = meminfo.c
libmeminfo_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libmeminfo.la
endif

if ENABLE_MSR_INTERLAGOS
libmsr_la_SOURCES = msr_interlagos.c
libmsr_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libmsr_interlagos.la
endif

if ENABLE_PERFEVENT
libperfevent_la_SOURCES = perfevent.c
libperfevent_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libperfevent.la
endif

if ENABLE_PROCDISKSTATS
libprocdiskstats_la_SOURCES = procdiskstats.c
libprocdiskstats_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocdiskstats.la
endif

if ENABLE_PROCINTERRUPTS
libprocinterrupts_la_SOURCES = procinterrupts.c
libprocinterrupts_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocinterrupts.la
endif

if ENABLE_PROCNETDEV
libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnetdev.la
endif

if ENABLE_PROCNFS
libprocnfs_la_SOURCES = procnfs.c
libprocnfs_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnfs.la
endif

if ENABLE_PROCSENSORS
libprocsensors_la_SOURCES = procsensors.c
libprocsensors_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocsensors.la
endif

if ENABLE_PROCSTATUTIL
libprocstatutil_la_SOURCES = procstatutil.c
libprocstatutil_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocstatutil.la
endif

if ENABLE_PROCSTATUTIL2
libprocstatutil2_la_SOURCES = procstatutil2.c
libprocstatutil2_la_LIBADD = $(COMMON_LIBADD)
libprocstatutil2_la_CFLAGS = -I$(builddir) $(AM_CFLAGS)
pkglib_LTLIBRARIES += libprocstatutil2.la
endif

if ENABLE_SYSCLASSIB
libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD) -libmad -libumad
pkglib_LTLIBRARIES += libsysclassib.la
endif

if ENABLE_VMSTAT
libvmstat_la_SOURCES = vmstat.c
libvmstat_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libvmstat.la
endif

if ENABLE_ATASMART
libsampler_atasmart_la_SOURCES = sampler_atasmart.c
libsampler_atasmart_la_LIBADD = $(COMMON_LIBADD) -latasmart
pkglib_LTLIBRARIES += libsampler_atasmart.la
endif

