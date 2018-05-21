pkglib_LTLIBRARIES =

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ -I$(srcdir)/$(SAMPLER)
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

# libstr_map
# libstr_map_la_SOURCES = str_map.c str_map.h fnv_hash.h
# lib_LTLIBRARIES += libstr_map.la

if ENABLE_JOBID
AM_CFLAGS += -DENABLE_JOBID
JOBID_LIBFLAGS = ../libjobid.la
endif

#liblustre_sampler
liblustre_sampler_la_SOURCES = lustre_sampler.c lustre_sampler.h
liblustre_sampler_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += liblustre_sampler.la

# common libadd for all subclass of lustre_sampler
COMMON_LIBADD = $(CORE)/libldms.la  liblustre_sampler.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll

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
liblustre2_client_la_LIBADD = $(COMMON_LIBADD) $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += liblustre2_client.la
