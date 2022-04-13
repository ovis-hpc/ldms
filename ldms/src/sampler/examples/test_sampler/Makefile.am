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

if ENABLE_TEST_SAMPLER_LDMS_TEST
libtest_sampler_la_SOURCES = test_sampler.c
libtest_sampler_la_LIBADD = $(COMMON_LIBADD) $(top_builddir)/lib/src/ovis_json/libovis_json.la

pkglib_LTLIBRARIES += libtest_sampler.la
#dist_man7_MANS += Plugin_test_sampler.man
endif
