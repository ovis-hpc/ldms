pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_ARRAY_EXAMPLE
libarray_example_la_SOURCES = array_example.c
libarray_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libarray_example.la
dist_man7_MANS += Plugin_array_example.man

liball_example_la_SOURCES = all_example.c
liball_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += liball_example.la
dist_man7_MANS += Plugin_all_example.man
endif
