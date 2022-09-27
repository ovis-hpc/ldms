lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir) -I$(srcdir)/..

libovis_logincludedir = $(includedir)/ovis_log
libovis_loginclude_HEADERS = ovis_log.h

libovis_log_la_SOURCES = ovis_log.c ovis_log.h
libovis_log_la_LIBADD = ../ovis_ev/libovis_ev.la \
			../ovis_util/libovis_util.la
lib_LTLIBRARIES += libovis_log.la
