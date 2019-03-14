pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST = ldms-sensors-config

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

BASE_LIBLA = ../libsampler_base.la
COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_FILESINGLE
libfilesingle_la_SOURCES = filesingle.c
libfilesingle_la_LIBADD = $(COMMON_LIBADD) $(BASE_LIBLA)
libfilesingle_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libfilesingle.la
bin_SCRIPTS += ldms-sensors-config
dist_man7_MANS += Plugin_filesingle.man
dist_man1_MANS += ldms-sensors-config.man

TESTS = test_sensors test_lscpu
endif
