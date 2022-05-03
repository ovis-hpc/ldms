pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_CORETEMP
dist_man7_MANS += Plugin_coretemp.man
libcoretemp_la_SOURCES = coretemp.c
libcoretemp_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libcoretemp.la
endif
