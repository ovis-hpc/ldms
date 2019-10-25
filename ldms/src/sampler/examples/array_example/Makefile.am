pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../../core
SAMPLER= ../../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../../.. @OVIS_LIB_INCDIR_FLAG@ \
		-I$(srcdir)/../../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

BASE_LIBADD = ../../libsampler_base.la
COMMON_LIBADD = $(CORE)/libldms.la \
	    @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_ARRAY_EXAMPLE
libarray_example_la_SOURCES = array_example.c
libarray_example_la_LIBADD = $(BASE_LIBADD) $(COMMON_LIBADD)
libarray_example_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libarray_example.la
dist_man7_MANS += Plugin_array_example.man

liball_example_la_SOURCES = all_example.c
liball_example_la_LIBADD = $(BASE_LIBADD) $(COMMON_LIBADD)
liball_example_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += liball_example.la
dist_man7_MANS += Plugin_all_example.man
endif
