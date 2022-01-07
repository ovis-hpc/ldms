pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_PROCNETDEV
libprocnetdev2_la_SOURCES = procnetdev2.c
libprocnetdev2_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocnetdev2.la
endif
