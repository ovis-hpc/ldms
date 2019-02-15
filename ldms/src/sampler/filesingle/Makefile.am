pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST=ldms-sensors-config

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
AM_CFLAGS += -DENABLE_JOBID
endif

COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util -lcoll $(JOBID_LIBFLAGS)

if ENABLE_FILESINGLE
libfilesingle_la_SOURCES = filesingle.c
libfilesingle_la_LIBADD = $(COMMON_LIBADD)
libfilesingle_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libfilesingle.la
bin_SCRIPTS += ldms-sensors-config
dist_man7_MANS += Plugin_filesingle.man
dist_man1_MANS += ldms-sensors-config.man
endif
