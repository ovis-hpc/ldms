pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_LLNL_EDAC
libedac_la_SOURCES = edac.c
libedac_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libedac.la
dist_man7_MANS += Plugin_edac.man
endif

EXTRA_DIST = LLNL_LICENSE_EDAC
