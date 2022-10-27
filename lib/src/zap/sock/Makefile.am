pkglib_LTLIBRARIES = libzap_sock.la

AM_CFLAGS = -I$(srcdir)/../.. -I$(srcdir)/.. -I$(top_srcdir) -I../..

libzap_sock_la_SOURCES = zap_sock.c zap_sock.h
libzap_sock_la_CFLAGS = $(AM_CFLAGS)
libzap_sock_la_LIBADD =  ../libzap.la ../../coll/libcoll.la ../../ovis_event/libovis_event.la ../../ovis_log/libovis_log.la
libzap_sock_la_LDFLAGS = $(AM_LDFLAGS) -pthread
