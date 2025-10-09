include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS=


AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
STORE_LIBADD = $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la



if ENABLE_BLOB_STREAM
libblob_msg_writer_la_SOURCES = blob_msg_writer.c
libblob_msg_writer_la_LIBADD = $(STORE_LIBADD) -lovis_json
pkglib_LTLIBRARIES += libblob_msg_writer.la
dist_man7_MANS += ldms-sampler_blob_msg_writer.man

endif

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_blob_msg_writer.rst
