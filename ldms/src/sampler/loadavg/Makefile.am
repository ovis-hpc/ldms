ACLOCAL_AMFLAGS = -I m4
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util
ldmssamplerincludedir = $(includedir)/ldms/sampler
ldmssamplerinclude_HEADERS = 

if ENABLE_JOBID
AM_CFLAGS += -DENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
endif

if ENABLE_LOADAVG
libloadavg_la_SOURCES = loadavg.c
libloadavg_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
libloadavg_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libloadavg.la
dist_man7_MANS += Plugin_loadavg.man
endif
