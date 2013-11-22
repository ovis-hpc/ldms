lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

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

#liblustre2_mds
liblustre2_mds_la_SOURCES = lustre2_mds.c
liblustre2_mds_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre2_mds.la

#liblustre2_oss
liblustre2_oss_la_SOURCES = lustre2_oss.c
liblustre2_oss_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre2_oss.la

#liblustre2_client
liblustre2_client_la_SOURCES = lustre2_client.c
liblustre2_client_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += liblustre2_client.la
