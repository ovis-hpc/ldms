SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms -lplugattr @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

STORE_LIBADD = -lldms -lplugattr -lcoll -lovis_util

ldmsstoreincludedir = $(includedir)/ldms

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_flatfile.la
dist_man7_MANS += Plugin_store_flatfile.man
endif
