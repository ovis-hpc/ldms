SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = -lldms -lcoll -lovis_util

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS = 

if ENABLE_TUTORIAL_STORE
libstore_tutorial_la_SOURCES = store_tutorial.c
libstore_tutorial_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_tutorial.la
dist_man7_MANS += Plugin_store_tutorial.man
endif

EXTRA_DIST = Plugin_store_tutorial.man
