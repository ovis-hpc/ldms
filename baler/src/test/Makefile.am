bin_PROGRAMS =

AM_CFLAGS = -I$(top_srcdir)/src

bmap_test_SOURCES = bmap_test.c
bmap_test_LDADD = ../baler/libbaler.la ../baler/libbset.la \
	       ../baler/libbmem.la \
	       ../baler/libbmapper.la \
	       ../baler/libbutils.la \
	       -lpthread -ldl
bin_PROGRAMS += bmap_test

bset_test_SOURCES = bset_test.c
bset_test_LDADD = ../baler/libbset.la
bin_PROGRAMS += bset_test

bstr_dist_u32_test_SOURCES = bstr_dist_u32_test.c
bstr_dist_u32_test_LDADD = ../baler/libbutils.la
bin_PROGRAMS += bstr_dist_u32_test
