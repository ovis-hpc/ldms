lib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE)
STORE_LIBADD = $(CORE)/libldms.la -lcoll -lovis_util

if ENABLE_SOS
libstore_sos_la_SOURCES = store_sos.c
libstore_sos_la_CFLAGS = $(AM_CFLAGS) -I$(OVIS_LIB_INCDIR) -I$(SOS_INCDIR)
libstore_sos_la_LIBADD = $(STORE_LIBADD) -lsos -L$(SOS_LIBDIR) \
			 -L$(OVIS_LIB_LIBDIR)
lib_LTLIBRARIES += libstore_sos.la
endif

if ENABLE_FLATFILE
libstore_flatfile_la_SOURCES = store_flatfile.c
libstore_flatfile_la_CFLAGS = $(AM_CFLAGS) -I$(OVIS_LIB_INCDIR)
libstore_flatfile_la_LIBADD = $(STORE_LIBADD) -L$(OVIS_LIB_LIBDIR)
lib_LTLIBRARIES += libstore_flatfile.la
endif

if ENABLE_MYSQL
libstore_mysql_la_SOURCES = store_mysql.c
libstore_mysql_la_CFLAGS = $(AM_CFLAGS) -I$(MYSQL_INCDIR) -I$(OVIS_LIB_INCDIR)
libstore_mysql_la_LIBADD = $(STORE_LIBADD) -lmysqlclient -L$(MYSQL_LIBDIR) \
			   -L$(OVIS_LIB_LIBDIR)
lib_LTLIBRARIES += libstore_mysql.la
endif

if ENABLE_MYSQLBULK
libstore_mysqlbulk_la_SOURCES = store_mysqlbulk.c
libstore_mysqlbulk_la_CFLAGS = $(AM_CFLAGS) -I$(MYSQL_INCDIR) \
			       -I$(OVIS_LIB_INCDIR)
libstore_mysqlbulk_la_LIBADD = $(STORE_LIBADD) -lmysqlclient \
			       -L$(OVIS_LIB_LIBDIR) \
			       -L$(MYSQL_LIBDIR)
lib_LTLIBRARIES += libstore_mysqlbulk.la
endif

if ENABLE_CSV
libstore_csv_la_SOURCES = store_csv.c
libstore_csv_la_CFLAGS = $(AM_CFLAGS) -I$(OVIS_LIB_INCDIR)
libstore_csv_la_LIBADD = $(STORE_LIBADD) -L$(OVIS_LIB_LIBDIR)
lib_LTLIBRARIES += libstore_csv.la
endif
