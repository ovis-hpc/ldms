SUBDIRS =
lib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

ldmscoreincludedir = $(includedir)/omq
ldmscoreinclude_HEADERS = omq.h

libovis_omq_la_SOURCES = omq.c omq.h
libovis_omq_la_LIBADD = -lc
lib_LTLIBRARIES += libovis_omq.la

omq_test_SOURCES = omq_test.c omq.h
omq_test_CFLAGS = $(AM_CFLAGS) -g -O0
omq_test_LDADD = libovis_omq.la

sbin_PROGRAMS = omq_test
