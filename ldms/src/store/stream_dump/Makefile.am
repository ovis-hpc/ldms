lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

libstream_dump_la_SOURCES = stream_dump.c
libstream_dump_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstream_dump.la
