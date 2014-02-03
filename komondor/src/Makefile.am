SUBDIRS = . store test

AM_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @SOS_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ \
	     @SOS_LIBDIR_FLAG@

sbin_PROGRAMS = komondor

komondor_SOURCES = komondor.c
komondor_LDADD = -lcoll -lzap -lpthread -lsos -lovis_util -ldl

if ENABLE_OCM
komondor_CFLAGS = $(AM_CFLAGS) -DENABLE_OCM @OCM_INCDIR_FLAG@
komondor_LDADD += -locm
komondor_LDFLAGS = $(AM_LDFLAGS) @OCM_LIBDIR_FLAG@
endif

