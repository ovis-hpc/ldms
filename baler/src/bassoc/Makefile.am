lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../

libbassocimg_la_SOURCES = bassocimg.c bassocimg.h
libbassocimg_la_LIBADD = ../baler/libbaler.la
libbassocimg_la_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
libbassocimg_la_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
lib_LTLIBRARIES += libbassocimg.la

bassoc_SOURCES =  bassoc.c bassoc.h
bassoc_LDADD = ../baler/libbaler.la \
	       ../query/libbquery.la \
	       libbassocimg.la \
	       -lpthread
bassoc_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
bassoc_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
bin_PROGRAMS += bassoc

bassocimg2pgm_SOURCES = bassocimg2pgm.c
bassocimg2pgm_LDADD = ../baler/libbaler.la libbassocimg.la
bassocimg2pgm_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
bassocimg2pgm_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
bin_PROGRAMS += bassocimg2pgm

bassocimg_dump_SOURCES = bassocimg_dump.c
bassocimg_dump_LDADD = ../baler/libbaler.la libbassocimg.la
bassocimg_dump_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
bassocimg_dump_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
bin_PROGRAMS += bassocimg_dump

bassocimg_intersect_test_SOURCES = bassocimg_intersect_test.c
bassocimg_intersect_test_LDADD = ../baler/libbaler.la libbassocimg.la
bassocimg_intersect_test_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
bassocimg_intersect_test_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
bin_PROGRAMS += bassocimg_intersect_test

bassocimg_cache_dump_SOURCES = bassocimg_cache_dump.c
bassocimg_cache_dump_LDADD = ../baler/libbaler.la libbassocimg.la
bassocimg_cache_dump_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@
bassocimg_cache_dump_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@
bin_PROGRAMS += bassocimg_cache_dump
