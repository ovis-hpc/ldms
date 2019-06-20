# The mpi_profiler subdirectory really belongs
# in its own project repo with mpi & ldms shm_util library dependence.
# Located here for initial development purposes.

include ../common.am

SUBDIRS = shm_util mpi_profiler

pkglib_LTLIBRARIES = libshm_sampler.la

libshm_sampler_la_SOURCES = shm_sampler.c
libshm_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS)
libshm_sampler_la_LIBADD  = $(SAMPLER_LIBADD) ../../core/libldms.la \
			    -lovis_util -lovis_third -lm -lrt -lpthread \
			    $(JOBID_LIBFLAGS) shm_util/liblshm.la
libshm_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@
