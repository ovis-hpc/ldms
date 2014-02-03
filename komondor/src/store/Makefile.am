AM_CFLAGS = -I../ @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

lib_LTLIBRARIES =

libkstore_default_la_SOURCES = kstore_default.c
libkstore_default_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
libkstore_default_la_LIBADD = -lsos -lovis_util @SOS_LIBDIR_FLAG@
lib_LTLIBRARIES += libkstore_default.la
