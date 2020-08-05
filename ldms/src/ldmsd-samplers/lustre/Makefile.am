include ../common.am

pkglib_LTLIBRARIES =

CORE = ../../core

#liblustre_sampler
liblustre_sampler_la_SOURCES = lustre_sampler.c lustre_sampler.h
liblustre_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
liblustre_sampler_la_LIBADD = $(CORE)/libldms.la \
			      @LDFLAGS_GETTIME@ \
			      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			      $(top_builddir)/lib/src/coll/libcoll.la
liblustre_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)
pkglib_LTLIBRARIES += liblustre_sampler.la

# common flags/lib for all of lustre2_* samplers
LUSTRE2_CFLAGS = $(AM_CFLAGS) $(SAMPLER_CFLAGS)
LUSTRE2_LIBADD = $(CORE)/libldms.la  liblustre_sampler.la \
		@LDFLAGS_GETTIME@ \
		$(SAMPLER_LIBADD) \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la
LUSTRE2_LDFLAGS = $(SAMPLER_LDFLAGS)

# liblustre2_client
liblustre2_client_la_SOURCES = lustre2_client.c
liblustre2_client_la_CFLAGS = $(LUSTRE2_CFLAGS)
liblustre2_client_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_client_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_client.la

# liblustre2_mds
liblustre2_mds_la_SOURCES = lustre2_mds.c
liblustre2_mds_la_CFLAGS  = $(LUSTRE2_CFLAGS)
liblustre2_mds_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_mds_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_mds.la

#liblustre2_oss
liblustre2_oss_la_SOURCES = lustre2_oss.c
liblustre2_oss_la_CFLAGS = $(LUSTRE2_CFLAGS)
liblustre2_oss_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_oss_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_oss.la
