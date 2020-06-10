SUBDIRS =
lib_LTLIBRARIES =

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

BUILT_SOURCES = json_parser.h
AM_YFLAGS = -d

EXTRA_DIST = json_lexer.l json_parser.y

clean-local:
	$(RM) json_parser.h json_parser.c json_lexer.c

json_parser.c: json_parser.h json_util.h

json_parser.h: json_parser.y
	$(BISON) --defines=json_parser.h -o json_parser.c $<

json_lexer.c: json_lexer.l json_util.h json_parser.h
	$(LEX) -o $@ $<

ldmscoreincludedir = $(includedir)/json
ldmscoreinclude_HEADERS = json_util.h

nodist_libjson_util_la_SOURCES = json_lexer.c json_parser.c json_parser.h
libjson_util_la_SOURCES = json_util.c json_util.h
libjson_util_la_LIBADD = ../coll/libcoll.la -lc -lcrypto ../third/libovis_third.la
lib_LTLIBRARIES += libjson_util.la

json_test_SOURCES = json_test.c json_util.h
json_test_CFLAGS = $(AM_CFLAGS) -g -O0
json_test_LDADD = libjson_util.la

sbin_PROGRAMS = json_test
