lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

libdecomp_static_la_SOURCES = decomp_static.c
libdecomp_static_la_LIBADD  = ../../core/libldms.la ../parsers/libdecomp_config_parser.la
pkglib_LTLIBRARIES += libdecomp_static.la
