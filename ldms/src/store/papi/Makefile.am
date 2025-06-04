include $(top_srcdir)/ldms/rules.mk


# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

if ENABLE_SOS
libstore_papi_la_SOURCES = store_papi.c
libstore_papi_la_CFLAGS = @SOS_INCDIR_FLAG@
libstore_papi_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_papi.la
dist_man7_MANS += ldms-store_papi.man
endif

CLEANFILES = $(dist_man7_MANS)
