pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../../core
SAMPLER= ../../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../../.. @OVIS_LIB_INCDIR_FLAG@ \
		-I$(srcdir)/../../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

BASE_LIBADD = ../../libldmsd_sampler_base.la
COMMON_LIBADD = $(CORE)/libldms.la \
	    @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_TEST_SAMPLER_LDMS_TEST
libtest_sampler_la_SOURCES = test_sampler.c
libtest_sampler_la_LIBADD = $(BASE_LIBADD) $(COMMON_LIBADD)
libtest_sampler_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libtest_sampler.la
#dist_man7_MANS += Plugin_test_sampler.man
endif
