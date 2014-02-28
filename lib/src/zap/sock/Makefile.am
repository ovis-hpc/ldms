lib_LTLIBRARIES = libzap_sock.la

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)/src

libzap_sock_la_SOURCES = zap_sock.c zap_sock.h
libzap_sock_la_CFLAGS = $(AM_CFLAGS) @EVENTINC@
libzap_sock_la_LIBADD = @EVENTLIBS@ ../libzap.la ../..//coll/libcoll.la
libzap_sock_la_LDFLAGS = $(AM_LDFLAGS) -pthread
