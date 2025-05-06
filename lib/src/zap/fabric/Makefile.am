pkglib_LTLIBRARIES = libzap_fabric.la

AM_CFLAGS = -I$(srcdir)/../.. -I$(srcdir)/.. -I$(top_srcdir) -I../..

libzap_fabric_la_SOURCES = zap_fabric.c zap_fabric.h
libzap_fabric_la_CFLAGS = @LIBFABRIC_CFLAGS@ $(AM_CFLAGS)
libzap_fabric_la_LIBADD = -lfabric @LIBFABRIC_LDFLAGS@ ../libzap.la \
			  ../../ovis_log/libovis_log.la
