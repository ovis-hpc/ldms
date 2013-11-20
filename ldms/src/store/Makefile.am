lib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I@OVIS_LIB_INCDIR@
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util \
		-L@OVIS_LIB_LIB64DIR@ \
		-L@OVIS_LIB_LIBDIR@

if ENABLE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libstore_sos_la_LIBADD = $(STORE_LIBADD) -lsos -L@SOS_LIB64DIR@ -L@SOS_LIBDIR@
lib_LTLIBRARIES += libstore_sos.la
endif

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_CFLAGS = $(AM_CFLAGS)
libstore_flatfile_la_LIBADD = $(STORE_LIBADD)
lib_LTLIBRARIES += libstore_flatfile.la
endif

if ENABLE_MYSQL
libstore_mysql_la_SOURCES = store_mysql.c
libstore_mysql_la_CFLAGS = $(AM_CFLAGS) @MYSQL_INCLUDE@
libstore_mysql_la_LIBADD = $(STORE_LIBADD) -lmysqlclient @MYSQL_LIBS@
lib_LTLIBRARIES += libstore_mysql.la
endif

if ENABLE_MYSQLBULK
libstore_mysqlbulk_la_SOURCES = store_mysqlbulk.c
libstore_mysqlbulk_la_CFLAGS = $(AM_CFLAGS) @MYSQL_INCLUDE@
libstore_mysqlbulk_la_LIBADD = $(STORE_LIBADD) -lmysqlclient \
			       @MYSQL_LIBS@
lib_LTLIBRARIES += libstore_mysqlbulk.la
endif

if ENABLE_CSV
libstore_csv_la_SOURCES = store_csv.c
libstore_csv_la_CFLAGS = $(AM_CFLAGS)
libstore_csv_la_LIBADD = $(STORE_LIBADD)
lib_LTLIBRARIES += libstore_csv.la
endif

if ENABLE_ME
libconsumer_me_la_SOURCES = consumer_me.c
libconsumer_me_la_CFLAGS = $(AM_CFLAGS)
libconsumer_me_la_LDFLAGS = $(AM_LDFLAGS)
libconsumer_me_la_LIBADD = $(STORE_LIBADD) -lzap
lib_LTLIBRARIES += libconsumer_me.la
endif
