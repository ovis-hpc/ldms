SUBDIRS = .

if ENABLE_RDMA
SUBDIRS += rdma
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

AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS = -I..
libzap_la_SOURCES = zap.c zap_priv.h
libzap_la_LIBADD = -ldl -lpthread
libzapincludedir = $(includedir)/zap
libzapinclude_HEADERS = zap.h

lib_LTLIBRARIES = libzap.la
