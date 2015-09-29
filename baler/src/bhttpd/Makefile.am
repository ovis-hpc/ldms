lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../

bhttpd_SOURCES = bq_fmt_json.c \
		 bq_fmt_json.h \
		 bhttpd_meta.c \
		 bhttpd_query.c \
		 bhttpd_list.c \
		 bhttpd.c bhttpd.h
bhttpd_LDADD = ../baler/libbaler.la \
	       ../query/libbquery.la \
	       -lpthread -levent -levent_pthreads -lzap
bhttpd_LDFLAGS = $(AM_LDFLAGS) @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@ \
		 @LIBEVENT_LIBDIR_FLAG@ @LIBEVENT_LIB64DIR_FLAG@
bhttpd_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
bin_PROGRAMS += bhttpd
