pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
CORE_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

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
