SUBDIRS = .

if ENABLE_RDMA
SUBDIRS += rdma
endif

if ENABLE_SOCK
SUBDIRS += sock
endif

if ENABLE_ZAPTEST
SUBDIRS += test
endif

libzap_la_SOURCES = zap.c zap_priv.h
libzap_la_LIBADD = -ldl
libzapincludedir = $(includedir)/zap
libzapinclude_HEADERS = zap.h

lib_LTLIBRARIES = libzap.la
