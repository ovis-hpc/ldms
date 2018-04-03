pkglib_LTLIBRARIES =
bin_PROGRAMS =
CORE = ../../core
AM_CFLAGS = -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
CC=$(MPICC)
CCLD=$(MPICC)

bin_PROGRAMS += MPIAppNoProfile
MPIAppNoProfile_SOURCES = sample_mpi_application.c
MPIAppNoProfile_CFLAGS = $(AM_CFLAGS)

bin_PROGRAMS += MPIApp
nodist_MPIApp_SOURCES = mpi_profiler_wrapped_functions.c
MPIApp_SOURCES = mpi_profiler.c mpi_profiler_configuration.c mpi_profiler_func_list.c sample_mpi_application.c userheader.h
MPIApp_CFLAGS = $(AM_CFLAGS) -v
MPIApp_LDADD = ../shm_util/liblshm.la -lovis_third -lpthread -lm

WRAPINPUT=$(srcdir)/wrap/input.w
mpi_profiler_wrapped_functions.c: Makefile wrap/wrap.py wrap/input.w userheader.h
	(python $(srcdir)/wrap/wrap.py -g -o mpi_profiler_wrapped_functions.c $(WRAPINPUT))

#lib_LTLIBRARIES = libldms_mpi_profiler.la
nodist_libldms_mpi_profiler_la_SOURCES = mpi_profiler_wrapped_functions.c
libldms_mpi_profiler_la_SOURCES = mpi_profiler.c mpi_profiler_configuration.c mpi_profiler_func_list.c userheader.h
libldms_mpi_profiler_la_LIBADD = $(LIBADD) $(JOBID_LIBFLAGS) ../shm_util/liblshm.la -lm -lrt -lpthread
pkglib_LTLIBRARIES += libldms_mpi_profiler.la
