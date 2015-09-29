AM_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@ -lpthread -lzap \
	     -levent -levent_pthreads -lcoll

ocmd_SOURCES = ocmd.c
ocmd_LDADD = ../ocm/libocm.la -ldl -lovis_util
sbin_PROGRAMS = ocmd
