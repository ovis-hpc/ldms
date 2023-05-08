SUBDIRS = .

if ENABLE_RDMA
SUBDIRS += rdma
endif

if HAVE_LIBFABRIC
SUBDIRS += fabric
endif

if ENABLE_SOCK
SUBDIRS += sock
endif

if ENABLE_UGNI
SUBDIRS += ugni
endif

if ENABLE_ZAPTEST
SUBDIRS += test
endif

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_CFLAGS = -DPLUGINDIR='"$(pkglibdir)"'
libzap_la_SOURCES = zap.c zap_priv.h
libzap_la_LIBADD = ../ovis_log/libovis_log.la \
		   -ldl -lpthread
libzapincludedir = $(includedir)/zap
libzapinclude_HEADERS = zap.h

lib_LTLIBRARIES = libzap.la
