include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
EXTRA_DIST =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll -lovis_log \
		@LDFLAGS_GETTIME@

libvariorum_sampler_la_SOURCES = variorum_sampler.c
libvariorum_sampler_la_LIBADD = $(COMMON_LIBADD) $(LTLIBVARIORUM) $(LTLIBJANSSON)
pkglib_LTLIBRARIES += libvariorum_sampler.la
dist_man7_MANS += ldms-sampler_variorum_sampler.man
EXTRA_DIST += README.md

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST+=ldms-sampler_variorum_sampler.rst
