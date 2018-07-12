lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../ldmsd
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS = store_csv_common.h

if ENABLE_AMQP
libstore_amqp_la_SOURCES = store_amqp.c
libstore_amqp_la_CFLAGS = $(AM_CFLAGS)
libstore_amqp_la_LIBADD = -lrabbitmq $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_amqp.la
endif

if ENABLE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_sos_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_sos.la
endif

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_CFLAGS = $(AM_CFLAGS)
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_flatfile.la
endif

if ENABLE_RABBITV3
libstore_rabbitv3_la_SOURCES = store_rabbitv3.c rabbit_utils.c rabbit_utils.h
libstore_rabbitv3_la_CFLAGS = $(AM_CFLAGS)
libstore_rabbitv3_la_LIBADD = -lrabbitmq $(STORE_LIBADD) @OVIS_AUTH_LIBS@
pkglib_LTLIBRARIES += libstore_rabbitv3.la
endif

if ENABLE_RABBITKW
libstore_rabbitkw_la_SOURCES = store_rabbitkw.c rabbit_utils.c rabbit_utils.h
libstore_rabbitkw_la_CFLAGS = $(AM_CFLAGS)
libstore_rabbitkw_la_LIBADD = -lrabbitmq $(STORE_LIBADD) @OVIS_AUTH_LIBS@
pkglib_LTLIBRARIES += libstore_rabbitkw.la
endif

if ENABLE_CSV
CSV_COMMON_LIBFLAGS = libldms_store_csv_common.la -lpthread

libldms_store_csv_common_la_SOURCES = store_csv_common.c store_csv_common.h
libldms_store_csv_common_la_CFLAGS = $(AM_CFLAGS)
libldms_store_csv_common_la_LIBADD = $(STORE_LIBADD) -lpthread
lib_LTLIBRARIES += libldms_store_csv_common.la

libstore_csv_la_SOURCES = store_common.h store_csv.c store_csv_common.h
libstore_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_csv_la_LIBADD = $(STORE_LIBADD) $(CSV_COMMON_LIBFLAGS)
pkglib_LTLIBRARIES += libstore_csv.la

libstore_function_csv_la_SOURCES = store_common.h store_function_csv.c
libstore_function_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_function_csv_la_LIBADD = $(STORE_LIBADD) -lpthread
pkglib_LTLIBRARIES += libstore_function_csv.la

endif

if ENABLE_ME
libconsumer_me_la_SOURCES = consumer_me.c
libconsumer_me_la_CFLAGS = $(AM_CFLAGS)
libconsumer_me_la_LDFLAGS = $(AM_LDFLAGS)
libconsumer_me_la_LIBADD = $(STORE_LIBADD) -lzap
lib_LTLIBRARIES += libconsumer_me.la
endif
