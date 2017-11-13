lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir) -I..

if ENABLE_OVIS_EVENT
libovis_event_la_SOURCES = ovis_event.c ovis_event.h ovis_event_priv.h
libovis_eventinclude_HEADERS = ovis_event.h
libovis_eventincludedir = $(includedir)/ovis_event
lib_LTLIBRARIES += libovis_event.la
endif

if ENABLE_OVIS_EVENT_TEST
ovis_event_test_SOURCES = ovis_event_test.c
ovis_event_test_LDADD = libovis_event.la -lpthread
bin_PROGRAMS += ovis_event_test

ovis_event_periodic_test_SOURCES = ovis_event_periodic_test.c
ovis_event_periodic_test_LDADD = libovis_event.la -lpthread
bin_PROGRAMS += ovis_event_periodic_test

ovis_event_net_test_SOURCES = ovis_event_net_test.c
ovis_event_net_test_LDADD = libovis_event.la -lpthread
bin_PROGRAMS += ovis_event_net_test
endif
