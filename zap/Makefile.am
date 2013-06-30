ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = autogen.sh

AM_CFLAGS = -Wall -g

src_libzap_la_SOURCES = src/zap.c

libzapincludedir = $(includedir)/zap
libzapinclude_HEADERS = src/zap.h

lib_LTLIBRARIES = src/libzap.la

dist-hook: libzap.spec
	cp libzap.spec $(distdir)

