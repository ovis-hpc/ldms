pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =
man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_KGNILND
libkgnilnd_la_SOURCES = kgnilnd.c
libkgnilnd_la_LIBADD =  $(BASE_LIBADD) $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libkgnilnd.la
man7_MANS += Plugin_kgnilnd.man
endif
EXTRA_DIST = Plugin_kgnilnd.man
