AM_CFLAGS = -I@top_srcdir@/src/
BALER = ../baler

bquery_SOURCES = bquery.c bquery.h bquery_priv.h
bquery_CFLAGS = $(AM_CFLAGS) -DBIN @SOS_INCDIR_FLAG@
bquery_LDADD = $(BALER)/libbaler.la -lsos @SOS_LIBDIR_FLAG@ \
	       ../plugins/libbsos_img.la \
	       ../plugins/libbsos_msg.la

bin_PROGRAMS = bquery

libbquery_la_SOURCES = bquery.c bquery.h bquery_priv.h
libbquery_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
libbquery_la_LDFLAGS = @SOS_LIBDIR_FLAG@ -Wl,-z,defs
libbquery_la_LIBADD = $(BALER)/libbaler.la -lsos \
		      ../plugins/libbsos_img.la \
		      ../plugins/libbsos_msg.la
lib_LTLIBRARIES = libbquery.la
