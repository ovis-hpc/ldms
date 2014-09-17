lib_LTLIBRARIES = libzap_ugni.la

AM_CFLAGS = -I$(srcdir)/../ -I$(srcdir)/../../

# UGNI_CLFAGS and UGNI_LIBS are set by PKG_CHECK_MODULES in configure.ac

libzap_ugni_la_SOURCES = zap_ugni.c zap_ugni.h
libzap_ugni_la_CFLAGS = $(AM_CFLAGS) $(UGNI_CFLAGS) $(RCA_CFLAGS) @EVENTINC@
libzap_ugni_la_LIBADD = ../libzap.la ../../coll/libcoll.la
libzap_ugni_la_LDFLAGS = $(UGNI_LIBS) $(RCA_LIBS) @EVENTLIBS@
