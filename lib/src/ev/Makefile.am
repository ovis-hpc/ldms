SUBDIRS =
lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..
AM_LDFLAGS = -L$(builddir) -pthread

ldmscoreincludedir = $(includedir)/ev
ldmscoreinclude_HEADERS = ev.h

libev_la_SOURCES = ev.c evw.c ev_priv.h
libev_la_CFLAGS = $(AM_CFLAGS) -g -O0
libev_la_LIBADD = -lc ../coll/libcoll.la
lib_LTLIBRARIES += libev.la

ev_test_SOURCES = ev_test.c
ev_test_CFLAGS = $(AM_CFLAGS) -g -O0
ev_test_LDADD = libev.la
ev_test_LDFLAGS = $(AM_LDFLAGS)
sbin_PROGRAMS = ev_test
