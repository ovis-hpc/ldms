lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../

if ENABLE_OVIS_UTIL
libovis_util_la_SOURCES = util.c util.h
libovis_utilinclude_HEADERS = util.h os_util.h
libovis_utilincludedir = $(includedir)/ovis_util
libovis_util_conf = /etc/ld.so.conf.d/libovis_util.conf
lib_LTLIBRARIES += libovis_util.la
endif
