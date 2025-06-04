include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_ARRAY_EXAMPLE
libarray_example_la_SOURCES = array_example.c
libarray_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libarray_example.la
dist_man7_MANS += ldms-sampler_array_example.man

liball_example_la_SOURCES = all_example.c
liball_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liball_example.la
dist_man7_MANS += ldms-sampler_all_example.man
endif

CLEANFILES = $(dist_man7_MANS)
