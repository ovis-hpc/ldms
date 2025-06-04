include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnetdev.la
dist_man7_MANS += ldms-sampler_procnetdev.man

CLEANFILES = $(dist_man7_MANS)
