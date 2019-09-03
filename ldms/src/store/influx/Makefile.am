# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core
LDMSD = ../../ldmsd
AM_CFLAGS = -I$(srcdir)/$(CORE) \
	    -I$(srcdir)/$(LDMSD) \
	    -I../.. \
	    @OVIS_LIB_INCDIR_FLAG@
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lcurl -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_INFLUX
libstore_influx_la_SOURCES = store_influx.c
libstore_influx_la_CFLAGS = $(AM_CFLAGS)
libstore_influx_la_LIBADD = $(STORE_LIBADD) -lcurl
pkglib_LTLIBRARIES += libstore_influx.la
endif

