include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS =

if ENABLE_TUTORIAL_STORE
libstore_tutorial_la_SOURCES = store_tutorial.c
libstore_tutorial_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_tutorial.la
dist_man7_MANS += ldms-store_tutorial.man
endif

CLEANFILES = $(dist_man7_MANS)
