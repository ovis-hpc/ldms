lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -lovis_util -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@

lib_LTLIBRARIES += libocmdsqlite3.la
libocmdsqlite3_la_SOURCES = ocmdsqlite3.c
libocmdsqlite3_la_LIBADD = -lsqlite3
