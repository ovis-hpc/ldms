lib_LTLIBRARIES =

AM_LDFLAGS=@OVIS_LIB_ABS@
AM_CPPFLAGS=@OVIS_INCLUDE_ABS@

check_PROGRAMS =
libmapinclude_HEADERS =
libmapincludedir = $(includedir)/coll

if ENABLE_COLL
libcoll_la_SOURCES = rbt.c rbt.h \
		     htbl.c htbl.h \
		     idx.c idx.h idx_priv.h \
		     str_map.c str_map.h fnv_hash.c fnv_hash.h \
		     ovis-map.h ovis-map.c \
		     label-set.h label-set.c \
		     heap.c heap.h
libcollinclude_HEADERS = rbt.h \
			 htbl.h \
			 idx.h \
			 str_map.h \
			 ovis-map.h \
			 label-set.h \
			 heap.h
libcollincludedir = $(includedir)/coll
libcoll_la_LIBADD = ../third/libovis_third.la ../ovis_util/libovis_util.la

lib_LTLIBRARIES += libcoll.la

check_PROGRAMS += test_ovis_map test_rbt test_label_set test_idx test_htbl

test_htbl_SOURCES = htbl.c htbl.h
test_htbl_CFLAGS = -DHTBL_TEST

test_rbt_SOURCES = rbt.c rbt.h
test_rbt_CFLAGS = -DRBT_TEST

test_ovis_map_SOURCES = test-ovis-map.c
test_ovis_map_LDADD = libcoll.la

test_label_set_SOURCES = test-label-set.c
#test_label_set_LDFLAGS = -static
#test_label_set_LDADD = libcoll.la ../ovis_util/libovis_util.la ../third/libovis_third.la
test_label_set_LDADD = libcoll.la

test_idx_SOURCES = idx.c
test_idx_CFLAGS = -DIDX_TEST

endif
