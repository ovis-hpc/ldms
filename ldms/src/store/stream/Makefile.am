SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS=


AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la



if ENABLE_HELLO_STREAM
libstream_csv_store_la_SOURCES = stream_csv_store.c
libstream_csv_store_la_LIBADD = $(STORE_LIBADD) -lovis_json
pkglib_LTLIBRARIES += libstream_csv_store.la
dist_man7_MANS += Plugin_stream_csv_store.man

endif

EXTRA_DIST = \
	Plugin_stream_csv_store.man
