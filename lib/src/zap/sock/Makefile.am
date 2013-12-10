lib_LTLIBRARIES = libzap_sock.la

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

libzap_sock_la_SOURCES = zap_sock.c zap_sock.h
libzap_sock_la_LIBADD = @EVENTLIBS@ -levent -levent_pthreads ../libzap.la
libzap_sock_la_LDFLAGS = -pthread
