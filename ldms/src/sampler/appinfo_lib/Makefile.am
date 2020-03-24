pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

libappinfocl_la_SOURCES = ldms_appinfo.c ldms_appinfo.h ldms_appinfo_shm.h
libappinfocl_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libappinfocl.la
