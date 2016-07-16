#AM_CFLAGS = -g -O0 -Wunused-result
#AM_LDFLAGS =

lib_LTLIBRARIES =
bin_PROGRAMS =

# A library for Baler-specific work functions, also includes codes from
# dependent libraries so that the build on Ubuntu is easier (no need to specify
# all libraries).
libbaler_la_SOURCES = bwqueue.c btkn.c bptn.c binput.c boutput.c \
		      bmapper.c bset.c bmvec.c bmem.c butils.c bhash.c \
		      bmhash.c \
		      bheap.c \
		      bmeta.c \
		      bqueue.c \
		      binput_private.h \
		      bmeta_priv.h \
		      fnv_hash.h \
		      murmur_hash.h
libbaler_la_LDFLAGS = $(AM_LDFLAGS) -lpthread
lib_LTLIBRARIES += libbaler.la

balerd_SOURCES = balerd.c
balerd_LDADD = libbaler.la \
	       -lcoll -levent -levent_pthreads -lzap -lpthread -ldl
balerd_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
balerd_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ @OVIS_LIB_LIB64DIR_FLAG@ @EVENTLIBS@

if ENABLE_OCM
balerd_LDADD += -locm
balerd_CFLAGS += -DENABLE_OCM @OCM_INCDIR_FLAG@
balerd_LDFLAGS += @OCM_LIBDIR_FLAG@
endif
bin_PROGRAMS += balerd

bmap_dump_SOURCES = bmap_dump.c
bmap_dump_LDADD = libbaler.la
bin_PROGRAMS += bmap_dump

bmeta_cluster_SOURCES = bmeta_cluster.c
bmeta_cluster_LDADD = libbaler.la
bin_PROGRAMS += bmeta_cluster

pkginclude_HEADERS = barray.h \
		     bcommon.h \
		     bhash.h \
		     bheap.h \
		     binput.h \
		     bmapper.h \
		     bmem.h \
		     bmeta.h \
		     bmhash.h \
		     bmlist.h \
		     bmvec.h \
		     boutput.h \
		     bplugin.h \
		     bptn.h \
		     bqueue.h \
		     bset.h \
		     btkn.h \
		     btkn_types.h \
		     btypes.h \
		     butils.h \
		     bwqueue.h
