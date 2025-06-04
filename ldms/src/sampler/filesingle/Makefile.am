include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST = ldms-sensors-config

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_FILESINGLE
libfilesingle_la_SOURCES = filesingle.c
libfilesingle_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libfilesingle.la
bin_SCRIPTS += ldms-sensors-config
dist_man7_MANS += ldms-sampler_filesingle.man
dist_man1_MANS += ldms-sensors-config.man

TESTS = test_sensors test_lscpu
endif

EXTRA_DIST += \
	test_sensors \
	test_lscpu \
	test_input

CLEANFILES = $(dist_man1_MANS) $(dist_man7_MANS)
