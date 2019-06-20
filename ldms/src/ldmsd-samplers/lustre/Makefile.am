include ../common.am

pkglib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../ldmsd

#liblustre_sampler
liblustre_sampler_la_SOURCES = lustre_sampler.c lustre_sampler.h
liblustre_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
liblustre_sampler_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  @LDFLAGS_GETTIME@ -lcoll
liblustre_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)
pkglib_LTLIBRARIES += liblustre_sampler.la

# common flags/lib for all of lustre2_* samplers
LUSTRE2_CFLAGS = $(SAMPLER_CFLAGS)
LUSTRE2_LIBADD = $(CORE)/libldms.la  liblustre_sampler.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll \
		$(SAMPLER_LIBADD)
LUSTRE2_LDFLAGS = $(SAMPLER_LDFLAGS)

# liblustre2_client
liblustre2_client_la_SOURCES = lustre2_client.c
liblustre2_client_la_CFLAGS = $(LUSTRE2_CFLAGS)
liblustre2_client_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_client_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_client.la

# liblustre2_mds
liblustre2_mds_la_SOURCES = lustre2_mds.c
liblsutre2_mds_la_CFLAGS = $(LUSTRE2_CFLAGS)
liblustre2_mds_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_mds_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_mds.la

#liblustre2_oss
liblustre2_oss_la_SOURCES = lustre2_oss.c
liblsutre2_oss_la_CFLAGS = $(LUSTRE2_CFLAGS)
liblustre2_oss_la_LIBADD = $(LUSTRE2_LIBADD)
liblustre2_oss_la_LDFLAGS = $(LUSTRE2_LDFLAGS)
pkglib_LTLIBRARIES += liblustre2_oss.la
