include $(top_srcdir)/ldms/rules.mk


SUBDIRS=test
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @DAOS_CFLAGS@
AM_LDFLAGS = @OVIS_LIB_ABS@ @DAOS_LDFLAGS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll -lovis_log

if ENABLE_DAOS_SAMPLER
libdaos_sampler_la_SOURCES = daos.h daos.c \
			     rank_target.h rank_target.c \
			     pool_target.h pool_target.c
libdaos_sampler_la_LIBADD = $(COMMON_LIBADD) -lgurt \
			    $(top_builddir)/ldms/src/sampler/libldms_compid_helper.la
pkglib_LTLIBRARIES += libdaos_sampler.la
dist_man7_MANS += ldms-daos-sampler.man
endif

CLEANFILES = $(dist_man7_MANS)
