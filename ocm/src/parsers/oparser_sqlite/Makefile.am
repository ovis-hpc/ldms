AM_CFLAGS = -I$(srcdir)/../ -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -lovis_util -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@

sbin_PROGRAMS = oparser_sqlite
oparser_sqlite_SOURCES = oparser_run.c template_parser.c \
				component_parser.c template_sql.c \
				scaffold_sql.c service_parser.c \
				model_event_parser.c oparser_util.c \
				oquery_sqlite.c
oparser_sqlite_LDADD = -lsqlite3 -lcoll
