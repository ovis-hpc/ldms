bin_PROGRAMS = ocmtest

AM_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@ -I$(top_srcdir)/src
AM_LDFLAGS = @EVENTLIBS@ -lpthread -lzap -levent -levent_pthreads \
	     @OVIS_LIB_LIBDIR_FLAG@

ocmtest_SOURCES = ocmtest.c
ocmtest_LDADD = ../ocm/libocm.la
