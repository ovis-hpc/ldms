lib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

libovis_util_la_SOURCES = util.c util.h dstring.h dstring.c
libovis_util_la_LIBADD = -lpthread -lrt
libovis_utilinclude_HEADERS = util.h dstring.h big_dstring.h os_util.h
libovis_utilincludedir = $(includedir)/ovis_util
libovis_util_conf = /etc/ld.so.conf.d/libovis_util.conf
lib_LTLIBRARIES += libovis_util.la
LDFLAGS_OVIS_UTIL = -lovis_util -lpthread @LDFLAGS_GETTIME@

noinst_PROGRAMS = test_big_dstring test_dstring

# pending port of slurm sampler
#test_rman_SOURCES= test_rman.c
#test_rman_LDFLAGS = -lovis_util @LDFLAGS_GETTIME@
#test_rman_DEPENDENCIES = libovis_util.la

test_dstring_SOURCES= test_dstring.c
test_dstring_LDADD = $(LDFLAGS_OVIS_UTIL)
test_dstring_DEPENDENCIES = libovis_util.la

test_util_SOURCES= test_util.c
test_util_LDFLAGS = @OVIS_LIB_ABS@
test_util_LDADD = $(LDFLAGS_OVIS_UTIL)
test_util_DEPENDENCIES = libovis_util.la
noinst_PROGRAMS += test_util
test_big_dstring_SOURCES=test_big_dstring.c big_dstring.h
test_big_dstring_LDADD = $(LDFLAGS_OVIS_UTIL)
test_big_dstring_LDFLAGS = @OVIS_LIB_ABS@
test_big_dstring_DEPENDENCIES = libovis_util.la
