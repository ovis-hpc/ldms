include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libperfevent_la_SOURCES = perfevent.c
libperfevent_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libperfevent.la
dist_man7_MANS += ldms-sampler_perfevent.man

CLEANFILES = $(dist_man7_MANS)
