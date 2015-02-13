lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../

bhttpd_SOURCES = bhttpd.c bq_fmt_json.c \
		 bhttpd_meta.c \
		 bhttpd_query.c
bhttpd_LDADD = ../baler/libbaler.la \
	       ../query/libbquery.la \
	       -lpthread -levent -lzap
bhttpd_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@
bhttpd_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@ @EVENTINC@
bin_PROGRAMS += bhttpd
