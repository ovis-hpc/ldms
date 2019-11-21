# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

AM_CFLAGS =  $(DBGFLAGS) -I$(srcdir)/$(SAMPLER)/slurm
STORE_LIBADD = -lldms -lcoll -lovis_util

if ENABLE_SOS
libstore_slurm_la_SOURCES = store_slurm.c
libstore_slurm_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_slurm_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_slurm.la
endif

