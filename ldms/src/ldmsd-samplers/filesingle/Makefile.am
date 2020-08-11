include ../common.am

pkglib_LTLIBRARIES = libfilesingle.la
libfilesingle_la_SOURCES = filesingle.c
libfilesingle_la_CFLAGS  = $(SAMPLER_CFLAGS)
libfilesingle_la_LIBADD  = $(SAMPLER_LIBADD) \
			   $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			   $(top_builddir)/lib/src/coll/libcoll.la
libfilesingle_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@


bin_SCRIPTS = ldms-sensors-config
dist_man7_MANS = Plugin_filesingle.man
dist_man1_MANS = ldms-sensors-config.man
EXTRA_DIST = ldms-sensors-config
