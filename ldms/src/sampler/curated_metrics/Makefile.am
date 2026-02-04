include $(top_srcdir)/ldms/rules.mk

pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_CURATED_METRICS
libcurated_metrics_la_SOURCES = curated_metrics.c
libcurated_metrics_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libcurated_metrics.la
endif

CLEANFILES = $(dist_man7_MANS)
