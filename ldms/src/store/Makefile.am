lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir)  @OVIS_LIB_INCDIR_FLAG@
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

## if ENABLE_SOS
## libstore_sos_la_SOURCES = store_sos.c
## libstore_sos_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
## libstore_sos_la_LIBADD = $(STORE_LIBADD) -lsos @SOS_LIB64DIR_FLAG@ @SOS_LIBDIR_FLAG@
## pkglib_LTLIBRARIES += libstore_sos.la
## endif

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_CFLAGS = $(AM_CFLAGS)
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_flatfile.la
endif

## if ENABLE_MYSQL
## libstore_mysql_la_SOURCES = store_mysql.c
## libstore_mysql_la_CFLAGS = $(AM_CFLAGS) @MYSQL_INCLUDE@
## libstore_mysql_la_LIBADD = $(STORE_LIBADD) -lmysqlclient @MYSQL_LIBS@
## pkglib_LTLIBRARIES += libstore_mysql.la
## endif

## if ENABLE_MYSQLBULK
## libstore_mysqlbulk_la_SOURCES = store_mysqlbulk.c
## libstore_mysqlbulk_la_CFLAGS = $(AM_CFLAGS) @MYSQL_INCLUDE@
## libstore_mysqlbulk_la_LIBADD = $(STORE_LIBADD) -lmysqlclient \
## 			       @MYSQL_LIBS@
## pkglib_LTLIBRARIES += libstore_mysqlbulk.la
## endif

if ENABLE_CSV
libstore_csv_la_SOURCES = store_csv.c
libstore_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_csv_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_csv.la

libstore_derived_csv_la_SOURCES = store_derived_csv.c
libstore_derived_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_derived_csv_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_derived_csv.la
endif
