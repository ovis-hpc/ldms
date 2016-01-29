lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

check_PROGRAMS =

if ENABLE_COLL
libcoll_la_SOURCES = rbt.c rbt.h \
		     idx.c idx.h idx_priv.h \
		     str_map.c str_map.h fnv_hash.h \
		     ovis-map.h ovis-map.c \
		     label-set.h label-set.c \
		     heap.c heap.h
libcollinclude_HEADERS = rbt.h \
			 idx.h \
			 str_map.h \
			 ovis-map.h \
			 label-set.h \
			 heap.h
libcollincludedir = $(includedir)/coll
libcoll_la_LIBADD = -L../third -L../ovis_util -lovis_third -lovis_util

lib_LTLIBRARIES += libcoll.la

check_PROGRAMS += test_ovis_map test_rbt test_label_set

test_rbt_SOURCES = rbt.c rbt.h
test_rbt_CFLAGS = $(AM_CFLAGS) -DRBT_TEST

test_ovis_map_SOURCES = test-ovis-map.c
test_ovis_map_CFLAGS = $(AM_CFLAGS)
#test_ovis_map_LDFLAGS =
#test_ovis_map_LDADD = libcoll.la ../third/libovis_third.la
test_ovis_map_LDADD = libcoll.la

test_label_set_SOURCES = test-label-set.c
test_label_set_CFLAGS = $(AM_CFLAGS)
#test_label_set_LDFLAGS = -static
#test_label_set_LDADD = libcoll.la ../ovis_util/libovis_util.la ../third/libovis_third.la
test_label_set_LDADD = libcoll.la

endif

#install-exec-hook:
#	echo $(libdir) > $(libcoll_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libcoll_conf)
#	ldconfig
