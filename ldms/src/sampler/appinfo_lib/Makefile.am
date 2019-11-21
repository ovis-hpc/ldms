pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

libappinfocl_la_SOURCES = ldms_appinfo.c ldms_appinfo.h ldms_appinfo_shm.h
libappinfocl_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libappinfocl.la
