pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =
check_PROGRAMS =
check_LTLIBRARIES =
TESTS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @DAOS_CFLAGS@
AM_LDFLAGS = @OVIS_LIB_ABS@ @DAOS_LDFLAGS@

if ENABLE_DAOS_SAMPLER
libdaos_sampler_mocks_la_SOURCES = mocks.h mocks.c
libdaos_sampler_mocks_la_LIBADD = $(COMMON_LIBADD)
check_LTLIBRARIES += libdaos_sampler_mocks.la

check_PROGRAMS += test_daos_sampler
test_daos_sampler_SOURCES = test_daos_sampler.c
test_daos_sampler_LDADD = -lsampler_base -L../.libs -L./.libs \
			  -ldaos_sampler -ldaos_sampler_mocks

TESTS += $(check_PROGRAMS)
endif
