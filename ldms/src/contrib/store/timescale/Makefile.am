include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
EXTRA_DIST =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS =

if ENABLE_TIMESCALE_STORE
libstore_timescale_la_SOURCES = store_timescale.c
libstore_timescale_la_LIBADD = $(STORE_LIBADD) $(LTLIBPQ)
pkglib_LTLIBRARIES += libstore_timescale.la
dist_man7_MANS += ldms-store_timescale.man
endif

EXTRA_DIST += example.conf
EXTRA_DIST += README.txt

CLEANFILES = $(dist_man7_MANS)
