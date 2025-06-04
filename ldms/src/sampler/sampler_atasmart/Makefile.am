include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libsampler_atasmart_la_SOURCES = sampler_atasmart.c
libsampler_atasmart_la_LIBADD = $(COMMON_LIBADD) -latasmart
pkglib_LTLIBRARIES += libsampler_atasmart.la
dist_man7_MANS += ldms-sampler_atasmart.man

CLEANFILES = $(dist_man7_MANS)
