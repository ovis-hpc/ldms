lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

if ENABLE_OVIS_UTIL
libovis_util_la_SOURCES = util.c util.h dstring.h dstring.c
libovis_utilinclude_HEADERS = util.h dstring.h big_dstring.h
libovis_utilincludedir = $(includedir)/ovis_util
libovis_util_conf = /etc/ld.so.conf.d/libovis_util.conf
lib_LTLIBRARIES += libovis_util.la

bin_PROGRAMS = test_big_dstring test_dstring

test_dstring_SOURCES= test_dstring.c 
test_dstring_LDFLAGS = -lovis_util
test_big_dstring_SOURCES=test_big_dstring.c big_dstring.h 
test_big_dstring_LDFLAGS = -lovis_util
endif
