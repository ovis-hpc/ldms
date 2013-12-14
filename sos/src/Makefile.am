INCLUDES = -I$(top_srcdir) -I@OVIS_LIB_INCLUDE@ 

sbin_PROGRAMS = \
	sos \
	sos_rate \
	sos_stat \
	oidx \
	oidx_dump \
	ods_dump \
	mds_load \
	mds_query

lib_LTLIBRARIES = \
	libods.la \
	libidx.la \
	liboidx.la \
	libsos.la

AM_CFLAGS = -Wall $(INCLUDES)

#
# libsos
#
libsos_la_SOURCES = \
	sos.c \
	sos.h \
	sos_priv.h
libsos_la_LIBADD = \
	libods.la \
	liboidx.la \
	libidx.la
#libsos_la_DEPENDENCIES = \
#	libsos.map \
#	libods.la \
#	liboidx.la

# Narate: I think it is a good idea to also export mds.h
libsosincludedir = $(includedir)/sos
libsosinclude_HEADERS = sos.h mds.h

#
# libods
#
libods_la_SOURCES = \
	ods.c \
	ods.h \
	ods_priv.h
# libods_la_LIBADD =
# libods_la_DEPENDENCIES = libods.map

libodsincludedir = $(includedir)/sos
libodsinclude_HEADERS = ods.h

#
# liboidx
#
liboidx_la_SOURCES = \
	oidx.c \
	oidx.h \
	oidx_priv.h
liboidx_la_LIBADD = \
	libidx.la \
	libods.la
#liboidx_la_DEPENDENCIES = \
#	libidx.la \
#	libods.la \
#	liboidx.map

liboidxincludedir = $(includedir)/sos
liboidxinclude_HEADERS = oidx.h

#
# libidx
#
libidx_la_SOURCES = \
	idx.c \
	idx.h \
	idx_priv.h
# libidx_la_LIBADD =
# libidx_la_DEPENDENCIES = libidx.map

libidxincludedir = $(includedir)/sos
libidxinclude_HEADERS = idx.h

sos_SOURCES = sos.c
sos_CFLAGS = -DSOS_MAIN $(INCLUDES)
sos_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	libidx.la

sos_rate_SOURCES = sos_rate.c
sos_rate_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	libidx.la

sos_stat_SOURCES = sos_stat.c
sos_stat_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	libidx.la

oidx_SOURCES = oidx.c
oidx_CFLAGS = -DOIDX_MAIN $(INCLUDES)
oidx_LDADD = \
	libods.la \
	libidx.la

mds_load_SOURCES = mds_load.c mds.h
mds_load_LDADD = \
	libsos.la \
	libidx.la \
	liboidx.la \
	libods.la
mds_load_LDFLAGS = -lpthread

ods_dump_SOURCES = ods_dump.c
ods_dump_LDADD = libods.la

oidx_dump_SOURCES = oidx_dump.c
oidx_dump_LDADD = liboidx.la libods.la

mds_query_SOURCES = mds_query.c mds.h
mds_query_LDADD = libsos.la \
        liboidx.la \
        libods.la


libsos_conf = /etc/ld.so.conf.d/libsos.conf

all-local:
	(mkdir -p tmp && cd tmp && if test !  -h sos; then $(LN_S) @abs_srcdir@ sos; fi)

clean-local:
	rm tmp/sos; rmdir tmp

# This has been disabled for now, to support the case that users want to
# install sos locally (not system-wide).
#
# For ldconfig
# install-exec-hook:
#	echo $(libdir) > $(libsos_conf)
#	ldconfig

#uninstall-local:
#	rm -f $(libsos_conf)
#	ldconfig

