pkglib_LTLIBRARIES =

# The mpi_profiler subdirectory really belongs
# in its own project repo with mpi & ldms shm_util library dependence.
# Located here for initial development purposes.

BASE_SRC=../sampler_base.c ../sampler_base.h

SUBDIRS= shm_util mpi_profiler

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. -I$(srcdir)/../../ldmsd @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util -lovis_third

libshm_sampler_la_SOURCES = shm_sampler.c $(BASE_SRC)
libshm_sampler_la_LIBADD = $(LIBADD) $(JOBID_LIBFLAGS) shm_util/liblshm.la -lm -lrt -lpthread
pkglib_LTLIBRARIES += libshm_sampler.la
