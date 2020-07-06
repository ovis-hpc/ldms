SUBDIRS =
lib_LTLIBRARIES =

CFLAGS := $(filter-out -Werror, ${CFLAGS})

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

BUILT_SOURCES = ovis_json_parser.h
AM_YFLAGS = -d

EXTRA_DIST = ovis_json_lexer.l ovis_json_parser.y

clean-local:
	$(RM) ovis_json_parser.h ovis_json_parser.c ovis_json_lexer.c

ovis_json_parser.c: ovis_json_parser.h ovis_json.h

ovis_json_parser.h: ovis_json_parser.y
	$(BISON) --defines=ovis_json_parser.h -o ovis_json_parser.c $<

ovis_json_lexer.c: ovis_json_lexer.l ovis_json.h ovis_json_parser.h
	$(LEX) -o $@ $<

ldmscoreincludedir = $(includedir)/ovis_json
ldmscoreinclude_HEADERS = ovis_json.h

nodist_libovis_json_la_SOURCES = ovis_json_lexer.c ovis_json_parser.c ovis_json_parser.h
libovis_json_la_SOURCES = ovis_json.c ovis_json.h
libovis_json_la_LIBADD = ../coll/libcoll.la -lc -lcrypto ../third/libovis_third.la
lib_LTLIBRARIES += libovis_json.la

ovis_json_test_SOURCES = ovis_json_test.c ovis_json.h
ovis_json_test_CFLAGS = $(AM_CFLAGS) -g -O0
ovis_json_test_LDADD = libovis_json.la

sbin_PROGRAMS = ovis_json_test
