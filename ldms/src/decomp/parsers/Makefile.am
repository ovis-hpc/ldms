include $(top_srcdir)/ldms/rules.mk

lib_LTLIBRARIES = libdecomp_config_parser.la

# For ldmsd_decomp_config_parser.h, which is in ldms/src/ldmsd
AM_CPPFLAGS = -I$(top_srcdir)/ldms/src/ldmsd

# Parser core plus one backend per available library.  The default backend
# is the first built of libfyaml, libyaml, jansson; LDMSD_DECOMP_PARSER
# selects another at run time.
# The public header, ldmsd_decomp_config_parser.h, lives in ldms/src/ldmsd next to
# ldmsd.h (which includes it) and is installed from there.
libdecomp_config_parser_la_SOURCES = decomp_config_parser.c decomp_parser_priv.h
libdecomp_config_parser_la_CFLAGS = $(AM_CFLAGS)
libdecomp_config_parser_la_LIBADD =

if HAVE_LIBFYAML
libdecomp_config_parser_la_SOURCES += decomp_parser_libfyaml.c
libdecomp_config_parser_la_CFLAGS += -DDECOMP_PARSER_LIBFYAML @LIBFYAML_CFLAGS@
libdecomp_config_parser_la_LIBADD += @LIBFYAML_LDFLAGS@
endif

if HAVE_LIBYAML
libdecomp_config_parser_la_SOURCES += decomp_parser_libyaml.c
libdecomp_config_parser_la_CFLAGS += -DDECOMP_PARSER_LIBYAML @LIBYAML_CFLAGS@
libdecomp_config_parser_la_LIBADD += @LIBYAML_LDFLAGS@
endif

if HAVE_LIBJANSSON
libdecomp_config_parser_la_SOURCES += decomp_parser_jansson.c
libdecomp_config_parser_la_CFLAGS += -DDECOMP_PARSER_JANSSON $(INCJANSSON)
libdecomp_config_parser_la_LIBADD += $(LTLIBJANSSON)
endif

# Unit tests: test_parser runs once for each parser that was built
check_PROGRAMS = test_parser
test_parser_SOURCES = test/test_parser.c
test_parser_LDADD = libdecomp_config_parser.la
test_parser_CFLAGS = $(AM_CFLAGS)

# Test data files
EXTRA_DIST = test/test_as_is.json \
             test/test_as_is.yaml \
             test/test_static.json \
             test/test_static.yaml \
             test/test_static_complex.json \
             test/test_static_complex.yaml \
             test/test_flex.json \
             test/test_flex_full.json \
             test/test_flex_full.yaml \
             test/test_bad_syntax.json \
             test/test_bad_syntax.yaml \
             test/test_dup_key.json \
             test/test_alias.yaml \
             test/test_scalars.json \
             test/test_s64_max.json \
             test/test_big_int.json \
             test/run_test_parser.sh

TESTS = test/run_test_parser.sh
TESTS_ENVIRONMENT = srcdir=$(srcdir) TEST_PARSER=./test_parser
