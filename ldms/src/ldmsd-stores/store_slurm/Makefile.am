include ../common.am

pkglib_LTLIBRARIES = libstore_slurm.la

libstore_slurm_la_SOURCES = store_slurm.c
libstore_slurm_la_CFLAGS  = $(STORE_CFLAGS) @SOS_INCDIR_FLAG@
libstore_slurm_la_LIBADD  = $(STORE_LIBADD)
libstore_slurm_la_LDFLAGS = $(STORE_LDFLAGS) @SOS_LIBDIR_FLAG@ \
                            @SOS_LIB64DIR_FLAG@ -lsos
