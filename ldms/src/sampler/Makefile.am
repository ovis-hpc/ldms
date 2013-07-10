ACLOCAL_AMFLAGS = -I m4
SUBDIRS = .
lib_LTLIBRARIES =
CORE = ../core

AM_CFLAGS = -I$(srcdir)/$(CORE)
COMMON_LIBADD = $(CORE)/libldms.la -lrt -lovis_util

if ENABLE_GEMINFO
libgeminfo_la_SOURCES = geminfo.c
libgeminfo_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libgeminfo.la
endif

if ENABLE_LUSTRE
SUBDIRS += lustre
endif

if ENABLE_MEMINFO
libmeminfo_la_SOURCES = meminfo.c
libmeminfo_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libmeminfo.la
endif

if ENABLE_PERFEVENT
libperfevent_la_SOURCES = perfevent.c
libperfevent_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libperfevent.la
endif

if ENABLE_PROCDISKSTATS
libprocdiskstats_la_SOURCES = procdiskstats.c
libprocdiskstats_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocdiskstats.la
endif

if ENABLE_PROCINTERRUPTS
libprocinterrupts_la_SOURCES = procinterrupts.c
libprocinterrupts_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocinterrupts.la
endif

if ENABLE_PROCNETDEV
libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocnetdev.la
endif

if ENABLE_PROCNFS
libprocnfs_la_SOURCES = procnfs.c
libprocnfs_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocnfs.la
endif

if ENABLE_PROCSENSORS
libprocsensors_la_SOURCES = procsensors.c
libprocsensors_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocsensors.la
endif

if ENABLE_PROCSTATUTIL
libprocstatutil_la_SOURCES = procstatutil.c
libprocstatutil_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocstatutil.la
endif

if ENABLE_GLIB
if ENABLE_SEDC
libsedc_la_SOURCES = sedc.c
libsedc_la_CFLAGS = $(AM_CFLAGS) $(GLIB20_CFLAGS)
libsedc_la_LIBADD = $(COMMON_LIBADD) $(GLIB20_LIBS)
lib_LTLIBRARIES += libsedc.la
endif
endif

if ENABLE_SYSCLASSIB
libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libsysclassib.la
endif

if ENABLE_VMSTAT
libvmstat_la_SOURCES = vmstat.c
libvmstat_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libvmstat.la
endif

