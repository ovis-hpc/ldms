include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
CORE_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
	      $(top_builddir)/ldms/src/core/libldms.la \
	      @LDFLAGS_GETTIME@ \
	      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
	      $(top_builddir)/lib/src/coll/libcoll.la

# libstr_map
# libstr_map_la_SOURCES = str_map.c str_map.h fnv_hash.h
# lib_LTLIBRARIES += libstr_map.la

#liblustre_sampler
liblustre_sampler_la_SOURCES = lustre_sampler.c lustre_sampler.h
liblustre_sampler_la_LIBADD = $(CORE_LIBADD)
pkglib_LTLIBRARIES += liblustre_sampler.la

# common libadd for all subclass of lustre_sampler
COMMON_LIBADD = liblustre_sampler.la $(CORE_LIBADD)

#liblustre2_mds
liblustre2_mds_la_SOURCES = lustre2_mds.c
liblustre2_mds_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liblustre2_mds.la

#liblustre2_oss
liblustre2_oss_la_SOURCES = lustre2_oss.c
liblustre2_oss_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liblustre2_oss.la

#liblustre2_client
liblustre2_client_la_SOURCES = lustre2_client.c
liblustre2_client_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liblustre2_client.la
dist_man7_MANS += ldms-sampler_lustre2_client.man

CLEANFILES = $(dist_man7_MANS)
