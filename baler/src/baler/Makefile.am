#AM_CFLAGS = -g -O0 -Wunused-result
#AM_LDFLAGS =

lib_LTLIBRARIES =
bin_PROGRAMS =

# Generic Baler utilities
libbutils_la_SOURCES = butils.c
libbutils_la_LIBADD = -lpthread
lib_LTLIBRARIES += libbutils.la

# Virtual Memory management utility
libbmem_la_SOURCES = bmem.c
libbmem_la_LIBADD = libbutils.la
lib_LTLIBRARIES += libbmem.la

# Virtual-memory vector
libbmvec_la_SOURCES = bmvec.c
libbmvec_la_LIBADD = libbmem.la libbutils.la
lib_LTLIBRARIES += libbmvec.la

# Virtual-memory unordered_map
libbmapper_la_SOURCES = bmapper.c
libbmapper_la_LIBADD = libbutils.la libbmem.la libbmvec.la
lib_LTLIBRARIES += libbmapper.la

# Convenient set implementation utility.
libbset_la_SOURCES = bset.c
libbset_la_LIBADD = libbutils.la
lib_LTLIBRARIES += libbset.la

# A library for Baler-specific work functions, also includes codes from
# dependent libraries so that the build on Ubuntu is easier (no need to specify
# all libraries).
libbaler_la_SOURCES = bwqueue.c btkn.c bptn.c binput.c boutput.c \
		      bmapper.c bset.c bmvec.c bmem.c butils.c bhash.c \
		      bmhash.c
libbaler_la_LDFLAGS = $(AM_LDFLAGS) -lpthread
lib_LTLIBRARIES += libbaler.la

balerd_SOURCES = balerd.c
balerd_LDADD = libbaler.la libbset.la \
	       libbmem.la \
	       libbmapper.la \
	       libbutils.la \
	       -lpthread -ldl -lcoll -levent -lzap
balerd_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @EVENTINC@
balerd_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@

if ENABLE_OCM
balerd_LDADD += -locm
balerd_CFLAGS += -DENABLE_OCM @OCM_INCDIR_FLAG@
balerd_LDFLAGS += @OCM_LIBDIR_FLAG@
endif
bin_PROGRAMS += balerd

bmap_dump_SOURCES = bmap_dump.c
bmap_dump_LDADD = libbaler.la
bin_PROGRAMS += bmap_dump

pkginclude_HEADERS = barray.h \
		     bcommon.h \
		     bhash.h \
		     binput.h \
		     bmapper.h \
		     bmem.h \
		     bmeta.h \
		     bmlist.h \
		     bmvec.h \
		     bmhash.h \
		     boutput.h \
		     bplugin.h \
		     bptn.h \
		     bset.h \
		     btkn.h \
		     btypes.h \
		     butils.h \
		     bwqueue.h
