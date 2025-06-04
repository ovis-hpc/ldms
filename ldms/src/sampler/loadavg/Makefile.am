include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_LOADAVG
libloadavg_la_SOURCES = loadavg.c
libloadavg_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libloadavg.la
dist_man7_MANS += ldms-sampler_loadavg.man
endif

CLEANFILES = $(dist_man7_MANS)
