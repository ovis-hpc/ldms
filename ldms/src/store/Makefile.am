include $(top_srcdir)/ldms/rules.mk


SUBDIRS = test
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = $(DBGFLAGS) @OVIS_INCLUDE_ABS@

STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS = store_csv_common.h

libstore_none_la_SOURCES = store_none.c
libstore_none_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_none.la

if ENABLE_AMQP
libstore_amqp_la_SOURCES = store_amqp.c
libstore_amqp_la_LIBADD = -lrabbitmq $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_amqp.la
endif

if ENABLE_KOKKOS
MAYBE_KOKKOS = kokkos kokkos_appmon
endif
SUBDIRS += $(MAYBE_KOKKOS)

if ENABLE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_sos_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_sos.la
dist_man7_MANS += ldms-store_sos.man

MAYBE_SLURM = slurm
MAYBE_PAPI = papi
MAYBE_STORE_APP = store_app
MAYBE_DARSHAN = darshan
MAYBE_PROC_STORE = proc_store
endif
SUBDIRS += $(MAYBE_SLURM)
SUBDIRS += $(MAYBE_PAPI)

if ENABLE_STORE_APP
SUBDIRS += $(MAYBE_STORE_APP)
endif

if ENABLE_DARSHAN
SUBDIRS += $(MAYBE_DARSHAN)
endif

if ENABLE_PROC_STREAMS
SUBDIRS += $(MAYBE_PROC_STORE)
endif

if ENABLE_INFLUX
MAYBE_INFLUX = influx
endif
SUBDIRS += $(MAYBE_INFLUX)

if ENABLE_FLATFILE
MAYBE_FLATFILE = store_flatfile
endif
SUBDIRS += $(MAYBE_FLATFILE)

if ENABLE_RABBITV3
libstore_rabbitv3_la_SOURCES = store_rabbitv3.c rabbit_utils.c rabbit_utils.h
libstore_rabbitv3_la_LIBADD = -lrabbitmq $(STORE_LIBADD) @OVIS_AUTH_LIBS@
pkglib_LTLIBRARIES += libstore_rabbitv3.la
dist_man7_MANS += ldms-store_rabbitv3.man
endif

if ENABLE_RABBITKW
libstore_rabbitkw_la_SOURCES = store_rabbitkw.c rabbit_utils.c rabbit_utils.h
libstore_rabbitkw_la_LIBADD = -lrabbitmq $(STORE_LIBADD) @OVIS_AUTH_LIBS@
pkglib_LTLIBRARIES += libstore_rabbitkw.la
dist_man7_MANS += ldms-store_rabbitkw.man
endif

if ENABLE_CSV
CSV_COMMON_LIBFLAGS = libldms_store_csv_common.la -lpthread

libldms_store_csv_common_la_SOURCES = store_csv_common.c store_csv_common.h
libldms_store_csv_common_la_LIBADD = $(STORE_LIBADD) -lpthread
lib_LTLIBRARIES += libldms_store_csv_common.la

libstore_csv_la_SOURCES = store_common.h store_csv.c store_csv_common.h
libstore_csv_la_LIBADD = $(STORE_LIBADD) $(CSV_COMMON_LIBFLAGS)
pkglib_LTLIBRARIES += libstore_csv.la

libstore_function_csv_la_SOURCES = store_common.h store_function_csv.c
libstore_function_csv_la_LIBADD = $(STORE_LIBADD) -lpthread
pkglib_LTLIBRARIES += libstore_function_csv.la
dist_man7_MANS += ldms-store_csv.man ldms-store_function_csv.man
endif

if ENABLE_HELLO_STREAM
SUBDIRS += stream
endif

if HAVE_KAFKA
SUBDIRS += kafka
endif

if ENABLE_STORE_AVRO_KAFKA
SUBDIRS += avro_kafka
endif

SUBDIRS += stream_dump

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-store_csv.rst ldms-store_function_csv.rst ldms-store_rabbitkw.rst ldms-store_rabbitv3.rst ldms-store_sos.rst
