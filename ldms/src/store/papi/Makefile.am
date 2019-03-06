# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../../core
LDMSD = ../../ldmsd
AM_CFLAGS = -I$(srcdir)/$(CORE) \
	    -I$(srcdir)/$(LDMSD) \
	    @OVIS_LIB_INCDIR_FLAG@
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_SOS
libstore_papi_la_SOURCES = store_papi.c
libstore_papi_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_papi_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_papi.la
endif

