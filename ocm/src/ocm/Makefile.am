AM_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@ -lpthread -lzap \
	     -levent -levent_pthreads -lcoll

libocm_la_SOURCES = ocm.c
lib_LTLIBRARIES = libocm.la

pkginclude_HEADERS = ocm.h
