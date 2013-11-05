ACLOCAL_AMFLAGS = -I m4
SUBDIRS = .
pkglib_LTLIBRARIES =
CORE = ../core

AM_CFLAGS = -I$(srcdir)/$(CORE)
COMMON_LIBADD = $(CORE)/libme_core.la

if ENABLE_STORE_EXAMPLE
libstore_example_la_SOURCES = store_example.c
libstore_example_la_CFLAGS = $(AM_CFLAGS)
libstore_example_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libstore_example.la
endif

if ENABLE_STORE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libstore_sos_la_LIBADD = $(COMMON_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_sos.la
endif

