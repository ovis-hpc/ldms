pkglib_LTLIBRARIES =
dist_man7_MANS =
EXTRA_DIST =
TESTS =

AM_LDFLAGS = @OVIS_LIB_ABS@ @GEOPM_LDFLAGS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @GEOPM_CFLAGS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_GEOPM_SAMPLER
libldms_geopm_sampler_la_SOURCES = ldms_geopm_sampler.c ldms_geopm_sampler.h
libldms_geopm_sampler_la_LIBADD = $(COMMON_LIBADD) -lgeopmd
pkglib_LTLIBRARIES += libldms_geopm_sampler.la
dist_man7_MANS += Plugin_geopm_sampler.man
ldms_geopm_sampler_test_SOURCES = ldms_geopm_sampler_test.c ldms_geopm_sampler.h
ldms_geopm_sampler_test_LDADD = libldms_geopm_sampler.la
check_PROGRAMS = ldms_geopm_sampler_test
TESTS += ldms_geopm_sampler_test.sh \
         ldms_geopm_sampler_test_domain.sh \
         ldms_geopm_sampler_test_index.sh \
         ldms_geopm_sampler_test_missing.sh \
         ldms_geopm_sampler_test_newline.sh \
         ldms_geopm_sampler_test_noent.sh \
         ldms_geopm_sampler_test_signal.sh \
         # end
else
EXTRA_DIST += ldms_geopm_sampler.c \
              ldms_geopm_sampler.h \
              ldms_geopm_sampler_test.c \
              ldms_geopm_sampler_test.sh \
              ldms_geopm_sampler_test_domain.sh \
              ldms_geopm_sampler_test_index.sh \
              ldms_geopm_sampler_test_missing.sh \
              ldms_geopm_sampler_test_newline.sh \
              ldms_geopm_sampler_test_noent.sh \
              ldms_geopm_sampler_test_signal.sh \
              # end
endif
EXTRA_DIST += .gitignore \
              COPYING-TPP \
              Plugin_geopm_sampler.man \
              README.md \
              ldms_geopm_sampler_test.conf \
              ldms_geopm_sampler_test.expect \
              ldms_geopm_sampler_test_domain.conf \
              ldms_geopm_sampler_test_domain.expect \
              ldms_geopm_sampler_test_index.conf \
              ldms_geopm_sampler_test_index.expect \
              ldms_geopm_sampler_test_missing.conf \
              ldms_geopm_sampler_test_missing.expect \
              ldms_geopm_sampler_test_newline.conf \
              ldms_geopm_sampler_test_newline.expect \
              ldms_geopm_sampler_test_noent.expect \
              ldms_geopm_sampler_test_signal.conf \
              ldms_geopm_sampler_test_signal.expect \
              # end
