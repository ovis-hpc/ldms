CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/.. @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = -L$(builddir) @OVIS_LIB_LIBDIR_FLAG@

BUILT_SOURCES = json_parser.h
AM_YFLAGS = -d

EXTRA_DIST = json_lexer.l json_parser.y

json_parser.c: json_parser.h json_util.h

json_parser.h: json_parser.y
	$(YACC) --defines=json_parser.h -o json_parser.c $<

json_lexer.c: json_lexer.l json_util.h json_parser.h
	$(LEX) -o $@ $<

nodist_libjson_util_la_SOURCES = json_lexer.c json_parser.c json_parser.h
libjson_util_la_SOURCES = json_util.c json_util.h
libjson_util_la_CFLAGS = $(AM_CFLAGS) -g -O0
libjson_util_la_LIBADD = -lc -lcrypto -lcoll
noinst_LTLIBRARIES = libjson_util.la

json_test_SOURCES = json_test.c json_util.h
json_test_CFLAGS = $(AM_CFLAGS) -g -O0
json_test_LDADD = libjson_util.la
json_test_LDFLAGS = $(AM_LDFLAGS)

check_PROGRAMS = json_test
