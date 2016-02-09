lib_LTLIBRARIES = libovis_third.la
# Here's where small 3rd party stuff with its own licenses and no
# commonly available packaged libraries is kept.
# We have added #ifdef usage to strip bits unused by ovis.
#

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

CITYSOURCE= city.c city.h citycrc.h
B64SOURCE= cencode.c cencode.h
B62SOURCE= b62encode.c b62encode.h

libovis_third_la_SOURCES= $(CITYSOURCE) $(B62SOURCE)
libovis_third_la_CFLAGS= -DCITYSLIM
libovis_third_la_LDFLAGS=
libovis_third_la_DEPENDENCIES=

check_PROGRAMS = city_test cencode_test b62_test

b62_test_SOURCES = b62encode-test.c
b62_test_LDADD = ../third/libovis_third.la
b62_test_LDFLAGS = -static

cencode_test_SOURCES = cencode-test.c $(B64SOURCE)
cencode_test_LDADD = ../third/libovis_third.la
cencode_test_LDFLAGS = -static

city_test_SOURCES = city-test.c
city_test_CFLAGS = -msse4.2 -O3 -DCITYSLIM
city_test_LDFLAGS = -static
city_test_LDADD = ../third/libovis_third.la
