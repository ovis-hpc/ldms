lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir)  @OVIS_LIB_INCDIR_FLAG@
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_sos_la_LIBADD = -lsos @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_sos.la
endif

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_CFLAGS = $(AM_CFLAGS)
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_flatfile.la
endif

if ENABLE_CSV
libstore_csv_la_SOURCES = store_common.h store_csv.c
libstore_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_csv_la_LIBADD = $(STORE_LIBADD) -lpthread
pkglib_LTLIBRARIES += libstore_csv.la

libstore_derived_csv_la_SOURCES = store_common.h store_derived_csv.c
libstore_derived_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_derived_csv_la_LIBADD = $(STORE_LIBADD) -lpthread
pkglib_LTLIBRARIES += libstore_derived_csv.la

endif

if ENABLE_ME
libconsumer_me_la_SOURCES = consumer_me.c
libconsumer_me_la_CFLAGS = $(AM_CFLAGS)
libconsumer_me_la_LDFLAGS = $(AM_LDFLAGS)
libconsumer_me_la_LIBADD = $(STORE_LIBADD) -lzap
lib_LTLIBRARIES += libconsumer_me.la
endif
