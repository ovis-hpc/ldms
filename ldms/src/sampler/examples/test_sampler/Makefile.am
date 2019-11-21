pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_TEST_SAMPLER_LDMS_TEST
libtest_sampler_la_SOURCES = test_sampler.c
libtest_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libtest_sampler.la
#dist_man7_MANS += Plugin_test_sampler.man
endif
