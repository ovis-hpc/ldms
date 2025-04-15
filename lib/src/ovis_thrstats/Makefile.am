lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..

libovis_thrstatsincludedir = $(includedir)/ovis_thrstats
libovis_thrstatsinclude_HEADERS = ovis_thrstats.h

libovis_thrstats_la_SOURCES = ovis_thrstats.c ovis_thrstats.h
libovis_thrstats_la_LIBADD = ../ovis_log/libovis_log.la \
			     -lpthread
lib_LTLIBRARIES += libovis_thrstats.la
