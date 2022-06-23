SUBDIRS =
lib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

BUILT_SOURCES = ovis_json_parser.h
AM_YFLAGS = -d

EXTRA_DIST = ovis_json_lexer.l ovis_json_parser.y


CLEANFILES = ovis_json_parser.h ovis_json_parser.c ovis_json_lexer.c

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

ovis_json_perf_test_SOURCES = ovis_json_perf_test.c ovis_json.h
ovis_json_perf_test_CFLAGS = $(AM_CFLAGS) -g -O3
ovis_json_perf_test_LDADD = libovis_json.la
EXTRA_DIST += $(srcdir)/input/ovis_json_perf_test.sh

sbin_PROGRAMS = ovis_json_test ovis_json_perf_test

installcheck-local: ovis_json_perf_test $(srcdir)/input/ovis_json_perf_test.sh
	LD_LIBRARY_PATH=$(DESTDIR)$(libdir) BIN=$(DESTDIR)$(sbindir) TESTBIN=. bash $(srcdir)/input/ovis_json_perf_test.sh
