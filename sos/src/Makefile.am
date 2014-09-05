INCLUDES = @OVIS_LIB_INCDIR_FLAG@

sbin_PROGRAMS = \
	bpt_verify \
	sos_verify \
	sos_archive \
	sos_combine \
	sos_restore \
	sos_reinit \
	ods_verify \
	bpt_test \
	sos \
	sos_test \
	sos_rate \
	sos_stat \
	sos_query \
	oidx \
	oidx_dump \
	ods_dump \
	mds_load \
	mds_query

lib_LTLIBRARIES = \
	libkey_STRING.la \
	libkey_BLOB.la \
	libkey_UINT64.la \
	libkey_UINT32.la \
	libods.la \
	libobjidx.la \
	liboidx.la \
	libidx_BPTREE.la \
	libsos.la

AM_CFLAGS = -Wall $(INCLUDES)
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ -Wl,-z,defs

#
# libsos
#
libsos_la_SOURCES = \
	sos.c \
	sos.h \
	sos_priv.h
libsos_la_LIBADD = \
	libods.la \
	libobjidx.la \
	liboidx.la \
	-lcoll \
	-lovis_util
#libsos_la_DEPENDENCIES = \
#	libsos.map \
#	libods.la \
#	liboidx.la

libsosincludedir = $(includedir)/sos
libsosinclude_HEADERS = sos.h mds.h ods.h obj_idx.h

#
# libidx_BPTREE
#
libidx_BPTREE_la_SOURCES = \
	bpt.c
libidx_BPTREE_la_LIBADD = \
	libobjidx.la \
	libods.la

#
# libidx_BPTREE
#
libkey_STRING_la_SOURCES = \
	key_str.c

libkey_BLOB_la_SOURCES = \
	key_blob.c

#
# libkey_UINT64
#
libkey_UINT64_la_SOURCES = \
	key_uint64.c

#
# libkey_UINT32
#
libkey_UINT32_la_SOURCES = \
	key_uint32.c

#
# libobjidx
#
libobjidx_la_SOURCES = \
	obj_idx.c \
	odj_idx.h
libobjidx_la_LIBADD = -ldl -lcoll \
		      libods.la
libobjidxincludedir = $(includedir)/sos
libobjidxinclude_HEADERS = ods.h

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
	-lcoll \
	libods.la
#liboidx_la_DEPENDENCIES = \
#	-lcoll \
#	libods.la \
#	liboidx.map

liboidxincludedir = $(includedir)/sos
liboidxinclude_HEADERS = oidx.h

sos_verify_SOURCES = sos_verify.c
sos_verify_CFLAGS = $(INCLUDES)
sos_verify_LDADD = \
		   libsos.la \
		   libods.la \
		   libobjidx.la

sos_archive_SOURCES = sos_archive.c
sos_archive_CFLAGS = $(INCLUDES)
sos_archive_LDADD = \
		   libsos.la \
		   libods.la \
		   libobjidx.la

sos_combine_SOURCES = sos_combine.c
sos_combine_CFLAGS = $(INCLUDES)
sos_combine_LDADD = \
		   libsos.la \
		   libods.la \
		   libobjidx.la

sos_restore_SOURCES = sos_restore.c
sos_restore_CFLAGS = $(INCLUDES)
sos_restore_LDADD = \
		   libsos.la \
		   libods.la \
		   libobjidx.la

sos_reinit_SOURCES = sos_reinit.c
sos_reinit_CFLAGS = $(INCLUDES)
sos_reinit_LDADD = \
		   libsos.la \
		   libods.la \
		   libobjidx.la

ods_verify_SOURCES = ods_verify.c
ods_verify_CFLAGS = $(INCLUDES)
ods_verify_LDADD = libods.la

bpt_verify_SOURCES = bpt_verify.c
bpt_verify_CFLAGS = $(INCLUDES)
bpt_verify_LDADD = \
		   libods.la \
		   libobjidx.la \
		   libidx_BPTREE.la

bpt_test_SOURCES = bpt_test.c
bpt_test_CFLAGS = $(INCLUDES)
bpt_test_LDADD = \
	libods.la \
	libobjidx.la \
	-lcoll

sos_SOURCES = sos.c
sos_CFLAGS = -DSOS_MAIN $(INCLUDES)
sos_LDADD = \
	-ldl \
	libsos.la \
	libobjidx.la \
	liboidx.la \
	libods.la \
	-lcoll \
	-lovis_util

sos_test_SOURCES = sos_test.c
sos_test_LDADD = \
		 libsos.la

sos_rate_SOURCES = sos_rate.c
sos_rate_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	-lcoll

sos_stat_SOURCES = sos_stat.c
sos_stat_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	-lcoll

sos_query_SOURCES = sos_query.c
sos_query_LDADD = \
	libsos.la \
	liboidx.la \
	libods.la \
	libobjidx.la \
	-lcoll

oidx_SOURCES = oidx.c
oidx_CFLAGS = -DOIDX_MAIN $(INCLUDES)
oidx_LDADD = \
	libods.la \
	-lcoll

mds_load_SOURCES = mds_load.c mds.h
mds_load_LDADD = \
	libsos.la \
	-lcoll
mds_load_LDFLAGS = $(AM_LDFLAGS) -lpthread

ods_dump_SOURCES = ods_dump.c
ods_dump_LDADD = libods.la

oidx_dump_SOURCES = oidx_dump.c
oidx_dump_LDADD = liboidx.la libods.la

mds_query_SOURCES = mds_query.c mds.h
mds_query_LDADD = libsos.la \
	libobjidx.la

libsos_conf = /etc/ld.so.conf.d/libsos.conf

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

