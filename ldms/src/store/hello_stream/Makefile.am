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
libhello_stream_store_la_SOURCES = hello_stream_store.c
libhello_stream_store_la_LIBADD = $(STORE_LIBADD) -lovis_json
pkglib_LTLIBRARIES += libhello_stream_store.la
dist_man7_MANS += Plugin_hello_stream_store.man

libl2_stream_csv_store_la_SOURCES = l2_stream_csv_store.c
libl2_stream_csv_store_la_LIBADD = $(STORE_LIBADD) -lovis_json
pkglib_LTLIBRARIES += libl2_stream_csv_store.la

endif

EXTRA_DIST = \
	Plugin_hello_stream_store.man
