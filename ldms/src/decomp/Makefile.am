include $(top_srcdir)/ldms/rules.mk

lib_LTLIBRARIES = libdecomp_config_parser.la

libdecomp_config_parser_la_SOURCES = decomp_config_parser.c decomp_config_parser.h
libdecomp_config_parser_la_LIBADD = @LIBYAML@
libdecomp_config_parser_la_CFLAGS = $(AM_CFLAGS)

# Install header to same location as ldmsd.h
ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = decomp_config_parser.h

# Unit tests
check_PROGRAMS = test_parser
test_parser_SOURCES = test/test_parser.c
test_parser_LDADD = libdecomp_config_parser.la
test_parser_CFLAGS = $(AM_CFLAGS)

# Test data files
EXTRA_DIST = ldmsd_decomposition.rst \
             test/test_as_is.json \
             test/test_as_is.yaml \
             test/test_static.json \
             test/test_static.yaml \
             test/test_static_complex.json \
             test/test_static_complex.yaml \
             test/test_flex.json \
             test/test_flex_full.json \
             test/test_flex_full.yaml

TESTS = test_parser

dist_man7_MANS = ldmsd_decomposition.man

# Build subdirectories after the library
SUBDIRS = .
SUBDIRS += static as_is flex

CLEANFILES = $(dist_man7_MANS)
