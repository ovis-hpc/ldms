lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

if ENABLE_OVIS_UTIL
libovis_util_la_SOURCES = util.c util.h dstring.h dstring.c rmaninfo.c spool.c
libovis_utilinclude_HEADERS = util.h dstring.h big_dstring.h rmaninfo.h spool.h log.h
libovis_utilincludedir = $(includedir)/ovis_util
libovis_util_conf = /etc/ld.so.conf.d/libovis_util.conf
libovis_util_la_LIBADD = -lrt
lib_LTLIBRARIES += libovis_util.la

check_PROGRAMS = test_big_dstring test_dstring test_olog test_sheller test_rman

test_rman_SOURCES= test_rman.c 
test_rman_LDFLAGS = -lovis_util -lrt
test_rman_DEPENDENCIES = libovis_util.la
test_dstring_SOURCES= test_dstring.c 
test_dstring_LDFLAGS = -lovis_util -lrt
test_dstring_DEPENDENCIES = libovis_util.la
test_big_dstring_SOURCES=test_big_dstring.c big_dstring.h 
test_big_dstring_LDFLAGS = -lovis_util -lrt
test_big_dstring_DEPENDENCIES = libovis_util.la
test_olog_SOURCES= test_olog.c 
test_olog_LDFLAGS = -lovis_util @LDFLAGS_GETTIME@
test_olog_DEPENDENCIES = libovis_util.la
test_sheller_SOURCES= test_sheller.c 
test_sheller_LDFLAGS = -lovis_util @LDFLAGS_GETTIME@
test_sheller_DEPENDENCIES = libovis_util.la
endif
