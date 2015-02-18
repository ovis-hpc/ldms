AM_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = -lovis_util @OVIS_LIB_LIBDIR_FLAG@

sbin_PROGRAMS = oparser_sqlite
oparser_sqlite_SOURCES = oparser_run.c template_parser.c \
				component_parser.c template_sql.c \
				scaffold_sql.c service_parser.c \
				model_event_parser.c oparser_util.c \
				oquery_sqlite.c cable_parser.c
oparser_sqlite_LDADD = -lsqlite3 -lcoll

SUBDIRS = python
