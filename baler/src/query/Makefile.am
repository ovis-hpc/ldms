AM_CFLAGS = -I@top_srcdir@/src/
BALER = ../baler

bquery_SOURCES = bquery.c bquery.h
bquery_CFLAGS = $(AM_CFLAGS) -DBIN -I@SOS_INCDIR@
bquery_LDADD = $(BALER)/libbaler.la -lsos -L@SOS_LIBDIR@

bin_PROGRAMS = bquery

libbquery_la_SOURCES = bquery.c bquery.h
libbquery_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libbquery_la_LIBADD = $(BALER)/libbaler.la -lsos -L@SOS_LIBDIR@
lib_LTLIBRARIES = libbquery.la
