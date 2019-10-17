SUBDIRS =
lib_LTLIBRARIES =

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..
AM_LDFLAGS = -L$(builddir)

BUILT_SOURCES = json_parser.h
AM_YFLAGS = -d

EXTRA_DIST = json_lexer.l json_parser.y

json_parser.c: json_parser.h json_util.h

json_parser.h: json_parser.y
	$(YACC) --defines=json_parser.h -o json_parser.c $<

json_lexer.c: json_lexer.l json_util.h json_parser.h
	$(LEX) -o $@ $<

ldmscoreincludedir = $(includedir)/json
ldmscoreinclude_HEADERS = json_util.h

nodist_libjson_util_la_SOURCES = json_lexer.c json_parser.c json_parser.h
libjson_util_la_SOURCES = json_util.c json_util.h $(srcdir)/../coll/htbl.c
libjson_util_la_CFLAGS = $(AM_CFLAGS) -g -O0
libjson_util_la_LIBADD = -lc -lcrypto
lib_LTLIBRARIES += libjson_util.la

json_test_SOURCES = json_test.c json_util.h
json_test_CFLAGS = $(AM_CFLAGS) -g -O0
json_test_LDADD = libjson_util.la
json_test_LDFLAGS = $(AM_LDFLAGS)

sbin_PROGRAMS = json_test

json_apis_test_SOURCES = json_apis_test.c json_util.h
json_apis_test_CFLAGS = $(AM_CFLAGS) -g -O0
json_apis_test_LDADD = -ljson_util
json_apis_test_LDFLAGS = $(AM_LDFLAGS)
sbin_PROGRAMS += json_apis_test
