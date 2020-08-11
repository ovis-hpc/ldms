# The mpi_profiler subdirectory really belongs
# in its own project repo with mpi & ldms shm_util library dependence.
# Located here for initial development purposes.

include ../common.am

EXTRA_DIST = README.md

SUBDIRS = shm_util mpi_profiler

pkglib_LTLIBRARIES = libshm_sampler.la

libshm_sampler_la_SOURCES = shm_sampler.c
libshm_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS)
libshm_sampler_la_LIBADD  = $(SAMPLER_LIBADD) ../../core/libldms.la \
			    $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			    $(top_builddir)/lib/src/ovis_third/libovis_third.la \
			    $(JOBID_LIBFLAGS) shm_util/liblshm.la \
			    -lm -lrt -lpthread
libshm_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@
