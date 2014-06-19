lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = -lovis_util @OVIS_LIB_LIBDIR_FLAG@

lib_LTLIBRARIES += libocmdsqlite3.la
libocmdsqlite3_la_SOURCES = ocmdsqlite3.c
libocmdsqlite3_la_LIBADD = -lsqlite3 ../libocm.la
