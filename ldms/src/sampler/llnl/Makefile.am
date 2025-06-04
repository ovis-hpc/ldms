include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_LLNL_EDAC
libedac_la_SOURCES = edac.c
libedac_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libedac.la
dist_man7_MANS += ldms-sampler_edac.man
endif

EXTRA_DIST = LLNL_LICENSE_EDAC

CLEANFILES = $(dist_man7_MANS)
