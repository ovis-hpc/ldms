bin_PROGRAMS = ocmtest

AM_CFLAGS = -I@OVIS_LIB_INCDIR@ -I$(top_srcdir)/src
AM_LDFLAGS = -lpthread -lzap -levent -levent_pthreads -L@OVIS_LIB_LIBDIR@

ocmtest_SOURCES = ocmtest.c
ocmtest_LDADD = ../libocm.la
