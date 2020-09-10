SUBDIRS =
lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..
AM_LDFLAGS = -L$(builddir) -pthread

libovis_evincludedir = $(includedir)/ovis_ev
libovis_evinclude_HEADERS = ev.h

libovis_ev_la_SOURCES = ev.c evw.c ev_priv.h
libovis_ev_la_CFLAGS = $(AM_CFLAGS)
libovis_ev_la_LIBADD = -lc ../coll/libcoll.la
lib_LTLIBRARIES += libovis_ev.la

if ENABLE_OVIS_EV_TEST
ovis_ev_test_SOURCES = ovis_ev_test.c
ovis_ev_test_CFLAGS = $(AM_CFLAGS)
ovis_ev_test_LDADD = libovis_ev.la
ovis_ev_test_LDFLAGS = $(AM_LDFLAGS)
sbin_PROGRAMS = ovis_ev_test
endif
