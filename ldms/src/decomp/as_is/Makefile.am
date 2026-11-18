lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

libdecomp_as_is_la_SOURCES = decomp_as_is.c
libdecomp_as_is_la_LIBADD  = ../../core/libldms.la ../libdecomp_config_parser.la
pkglib_LTLIBRARIES += libdecomp_as_is.la
