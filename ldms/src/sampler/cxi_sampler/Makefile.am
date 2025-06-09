include $(top_srcdir)/ldms/rules.mk

pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libcxi_sampler_la_SOURCES = cxi_sampler.c
libcxi_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libcxi_sampler.la
dist_man7_MANS += ldms-sampler_cxi_sampler.man
CLEANFILES = $(dist_man7_MANS)
