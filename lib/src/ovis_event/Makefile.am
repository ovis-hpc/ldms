lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

if ENABLE_OVIS_EVENT
libovis_event_la_SOURCES = ovis_event.c ovis_event.h
libovis_eventinclude_HEADERS = ovis_event.h
libovis_event_la_LIBADD = ../coll/libcoll.la
libovis_eventincludedir = $(includedir)/ovis_event
lib_LTLIBRARIES += libovis_event.la
endif

if ENABLE_OVIS_EVENT_TEST
ovis_event_test_SOURCES = ovis_event_test.c
ovis_event_test_LDADD = libovis_event.la -lpthread
bin_PROGRAMS += ovis_event_test
endif
