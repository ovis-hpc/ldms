SUBDIRS =
lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..
AM_LDFLAGS = -L$(builddir)

libovis_refincludedir = $(includedir)/ovis_ref
libovis_refinclude_HEADERS = ref.h

libovis_ref_la_SOURCES = ref.h
libovis_ref_la_CFLAGS = $(AM_CFLAGS)
lib_LTLIBRARIES += libovis_ref.la
