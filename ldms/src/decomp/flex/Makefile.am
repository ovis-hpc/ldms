lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

DECOMP_LIBADD = ../../core/libldms.la \
		../../ldmsd/libldmsd_request.la

libdecomp_flex_la_SOURCES = decomp_flex.c
libdecomp_flex_la_LIBADD  = $(DECOMP_LIBADD)
pkglib_LTLIBRARIES += libdecomp_flex.la
