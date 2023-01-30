pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libclock_la_SOURCES = clock.c
libclock_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libclock.la
dist_man7_MANS += Plugin_clock.man
