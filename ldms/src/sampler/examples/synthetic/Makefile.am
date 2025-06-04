include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../../core
SAMPLER= ../../../sampler
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la


if ENABLE_SYNTHETIC
libsynthetic_la_SOURCES = synthetic.c
libsynthetic_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libsynthetic.la
dist_man7_MANS += ldms-sampler_synthetic.man
endif

CLEANFILES = $(dist_man7_MANS)
