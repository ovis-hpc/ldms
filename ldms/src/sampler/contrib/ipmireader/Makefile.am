SUBDIRS =
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =


BASE_SRC=../../sampler_base.c ../../sampler_base.h
CORE = ../../../core
SAMPLER= ../../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@


COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util -lcoll


if ENABLE_IPMIREADER
libipmireader_la_SOURCES = ipmireader.c $(BASE_SRC)
libipmireader_la_LIBADD = $(COMMON_LIBADD)
libipmireader_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libipmireader.la
dist_man7_MANS += Plugin_ipmireader.man

libipmisensors_la_SOURCES = ipmisensors.c
libipmisensors_la_LIBADD = $(COMMON_LIBADD) $(BASE_SRC)
libipmisensors_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libipmisensors.la
dist_man7_MANS += Plugin_ipmisensors.man
endif
