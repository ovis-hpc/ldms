include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES = libstore_app.la
dist_man7_MANS =

libstore_app_la_SOURCES = store_app.c
libstore_app_la_CFLAGS = @SOS_INCDIR_FLAG@ @OVIS_INCLUDE_ABS@
libstore_app_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ @OVIS_LIB_ABS@ \
			 $(top_builddir)/ldms/src/core/libldms.la \
			 $(top_builddir)/lib/src/coll/libcoll.la \
			 $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			 -lsos

dist_man7_MANS += ldmsd_store_app.man

CLEANFILES = $(dist_man7_MANS)
