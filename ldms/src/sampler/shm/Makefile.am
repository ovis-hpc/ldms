include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =

# The mpi_profiler subdirectory really belongs
# in its own project repo with mpi & ldms shm_util library dependence.
# Located here for initial development purposes.

SUBDIRS= shm_util mpi_profiler

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/third/libovis_third.la

LIBADD = $(COMMON_LIBADD)

libshm_sampler_la_SOURCES = shm_sampler.c
libshm_sampler_la_LIBADD = $(LIBADD) $(JOBID_LIBFLAGS) shm_util/liblshm.la -lm -lrt -lpthread
pkglib_LTLIBRARIES += libshm_sampler.la
dist_man7_MANS = ldms-sampler_shm_sampler.man

EXTRA_DIST = \
	outline \
	README.md \
	test/example-mpi_profiler-conf.sh \
	test/samplerd.conf

CLEANFILES = $(dist_man7_MANS)
