SUBDIRS = . test

AM_CFLAGS = -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIBDIR@ -lpthread -lzap -levent -levent_pthreads -lcoll

libocm_la_SOURCES = ocm.c
lib_LTLIBRARIES = libocm.la

if ENABLE_OCMD
ocmd_SOURCES = ocmd.c
ocmd_LDADD = libocm.la -ldl -lovis_util
sbin_PROGRAMS = ocmd
SUBDIRS += plugins
endif

if ENABLE_PARSERS
SUBDIRS += parsers
endif

pkginclude_HEADERS = ocm.h
