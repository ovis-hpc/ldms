pkglib_LTLIBRARIES =

# The mpi_profiler subdirectory really belongs
# in its own project repo with mpi & ldms shm_util library dependence.
# Located here for initial development purposes.

SUBDIRS= shm_util mpi_profiler

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

LIBADD = $(COMMON_LIBADD) -lovis_third

libshm_sampler_la_SOURCES = shm_sampler.c
libshm_sampler_la_LIBADD = $(LIBADD) $(JOBID_LIBFLAGS) shm_util/liblshm.la -lm -lrt -lpthread
pkglib_LTLIBRARIES += libshm_sampler.la

EXTRA_DIST = \
	outline \
	README.md \
	test/example-mpi_profiler-conf.sh \
	test/samplerd.conf
