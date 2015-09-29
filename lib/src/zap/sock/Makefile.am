pkglib_LTLIBRARIES = libzap_sock.la

AM_CFLAGS = -I$(srcdir)/../.. -I$(srcdir)/.. -I$(top_srcdir) @LIBEVENT_INCDIR_FLAG@

libzap_sock_la_SOURCES = zap_sock.c zap_sock.h
libzap_sock_la_CFLAGS = $(AM_CFLAGS) 
libzap_sock_la_LIBADD =  ../libzap.la ../../coll/libcoll.la
libzap_sock_la_LDFLAGS = $(AM_LDFLAGS) @LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ $(EVENTLIBS) -pthread
