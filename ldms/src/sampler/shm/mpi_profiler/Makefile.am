pkglib_LTLIBRARIES =
bin_PROGRAMS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

CC=$(MPICC)
CCLD=$(MPICC)

bin_PROGRAMS += MPIAppNoProfile
MPIAppNoProfile_SOURCES = sample_mpi_application.c
#MPIAppNoProfile_CFLAGS = $(AM_CFLAGS)

bin_PROGRAMS += MPIApp
nodist_MPIApp_SOURCES = mpi_profiler_wrapped_functions.c
MPIApp_SOURCES = mpi_profiler.c mpi_profiler.h mpi_profiler_configuration.c mpi_profiler_configuration.h mpi_profiler_func_list.c mpi_profiler_func_list.h sample_mpi_application.c userheader.h
MPIApp_CFLAGS = $(AM_CFLAGS) -v
MPIApp_LDADD = ../shm_util/liblshm.la \
	       $(top_builddir)/lib/src/third/libovis_third.la \
	       -lpthread -lm

WRAPINPUT=$(srcdir)/wrap/input.w
mpi_profiler_wrapped_functions.c: Makefile wrap/wrap.py wrap/input.w userheader.h
	(python $(srcdir)/wrap/wrap.py -g -o mpi_profiler_wrapped_functions.c $(WRAPINPUT))
EXTRA_DIST=$(srcdir)/wrap/input.w $(WRAPINPUT) $(srcdir)/wrap/wrap.py

#lib_LTLIBRARIES = libldms_mpi_profiler.la
nodist_libldms_mpi_profiler_la_SOURCES = mpi_profiler_wrapped_functions.c
libldms_mpi_profiler_la_SOURCES = mpi_profiler.c mpi_profiler.h mpi_profiler_configuration.c mpi_profiler_configuration.h mpi_profiler_func_list.c mpi_profiler_func_list.h userheader.h
libldms_mpi_profiler_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS) ../shm_util/liblshm.la -lm -lrt -lpthread
pkglib_LTLIBRARIES += libldms_mpi_profiler.la
