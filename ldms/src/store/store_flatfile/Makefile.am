include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms -lldmsd_plugattr @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

ldmsstoreincludedir = $(includedir)/ldms

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_flatfile.la
dist_man7_MANS += ldms-store_flatfile.man
endif

CLEANFILES = $(dist_man7_MANS)
