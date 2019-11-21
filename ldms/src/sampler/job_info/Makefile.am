pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libjobinfo_la_SOURCES = jobinfo.c jobinfo.h
libjobinfo_la_LIBADD = $(COMMON_LIBADD)
libjobinfo_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libjobinfo.la
