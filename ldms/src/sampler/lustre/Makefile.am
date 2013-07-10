lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE)
# libstr_map
libstr_map_la_SOURCES = str_map.c str_map.h fnv_hash.h
lib_LTLIBRARIES += libstr_map.la

#liblustre_sampler
liblustre_sampler_la_SOURCES = lustre_sampler.c lustre_sampler.h
liblustre_sampler_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  libstr_map.la -lrt
lib_LTLIBRARIES += liblustre_sampler.la

# common libadd for all subclass of lustre_sampler
COMMON_LIBADD = $(CORE)/libldms.la  libstr_map.la liblustre_sampler.la \
		-lrt -lovis_util

#liblustre_mds
liblustre_mds_la_SOURCES = lustre_mds.c
liblustre_mds_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre_mds.la

#liblustre_oss
liblustre_oss_la_SOURCES = lustre_oss.c
liblustre_oss_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre_oss.la

#liblustre_client
liblustre_client_la_SOURCES = lustre_client.c
liblustre_client_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre_client.la
