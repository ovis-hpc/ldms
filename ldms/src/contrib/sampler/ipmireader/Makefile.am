include $(top_srcdir)/ldms/rules.mk


SUBDIRS =
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
EXTRA_DIST =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_log/libovis_log.la

if ENABLE_IPMIREADER
libipmireader_la_SOURCES = ipmireader.c
libipmireader_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libipmireader.la
dist_man7_MANS += ldms-sampler_ipmireader.man

libipmisensors_la_SOURCES = ipmisensors.c
libipmisensors_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libipmisensors.la
dist_man7_MANS += ldms-sampler_ipmisensors.man
endif

CLEANFILES = $(dist_man7_MANS)
