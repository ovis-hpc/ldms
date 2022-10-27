pkglib_LTLIBRARIES = libzap_ugni.la

AM_CFLAGS = -I$(srcdir)/../ -I$(srcdir)/../../ -I../..

# UGNI_CLFAGS and UGNI_LIBS are set by PKG_CHECK_MODULES in configure.ac

libzap_ugni_la_SOURCES = zap_ugni.c zap_ugni.h
libzap_ugni_la_CFLAGS = $(AM_CFLAGS) $(UGNI_CFLAGS) $(RCA_CFLAGS)
libzap_ugni_la_LIBADD = ../libzap.la \
			../../coll/libcoll.la \
			../../ovis_event/libovis_event.la \
			../../ovis_log/libovis_log.la
libzap_ugni_la_LDFLAGS = $(UGNI_LIBS) $(RCA_LIBS)
