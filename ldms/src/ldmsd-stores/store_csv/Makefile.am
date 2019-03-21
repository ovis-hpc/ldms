include ../common.am

pkglib_LTLIBRARIES =
lib_LTLIBRARIES =

LDMSD = ../../ldmsd
STORE_LIBADD += $(LDMSD)/libplugattr.la -lcoll -lovis_util -lpthread

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS = store_csv_common.h

libldms_store_csv_common_la_SOURCES = store_csv_common.c store_csv_common.h
libldms_store_csv_common_la_CFLAGS = $(STORE_CFLAGS)
libldms_store_csv_common_la_LIBADD = $(STORE_LIBADD)
libldms_store_csv_common_la_LDFLAGS = $(STORE_LDFLAGS)
lib_LTLIBRARIES += libldms_store_csv_common.la

libstore_csv_la_SOURCES = store_csv.c store_common.h store_csv_common.h
libstore_csv_la_CFLAGS = $(STORE_CFLAGS)
libstore_csv_la_LIBADD = $(STORE_LIBADD) libldms_store_csv_common.la
libstore_csv_la_LDFLAGS = $(STORE_LDFLAGS)
pkglib_LTLIBRARIES += libstore_csv.la

libstore_function_csv_la_SOURCES = store_common.h store_function_csv.c
libstore_function_csv_la_CFLAGS = $(STORE_CFLAGS)
libstore_function_csv_la_LIBADD = $(STORE_LIBADD)
libstore_function_csv_la_LDFLAGS = $(STORE_LDFLAGS)
pkglib_LTLIBRARIES += libstore_function_csv.la
