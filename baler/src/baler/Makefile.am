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
libbmapper_la_LIBADD = libbutils.la libbmem.la
lib_LTLIBRARIES += libbmapper.la

# Convenient set implementation utility.
libbset_la_SOURCES = bset.c
libbset_la_LIBADD = libbutils.la
lib_LTLIBRARIES += libbset.la

# A library for Baler-specific work functions, also includes codes from
# dependent libraries so that the build on Ubuntu is easier (no need to specify
# all libraries).
libbaler_la_SOURCES = bwqueue.c btkn.c bptn.c binput.c boutput.c \
		      bmapper.c bset.c bmvec.c bmem.c butils.c
# libbaler_la_LIBADD = libbmapper.la libbmvec.la libbset.la
lib_LTLIBRARIES += libbaler.la

balerd_SOURCES = balerd.c
balerd_LDADD = libbaler.la libbset.la \
	       libbmem.la \
	       libbmapper.la \
	       libbutils.la \
	       -lpthread -ldl -lcoll
balerd_CFLAGS = @OVIS_LIB_INCDIR_FLAG@
balerd_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_OCM
balerd_LDADD += -locm
balerd_CFLAGS += -DENABLE_OCM @OCM_INCDIR_FLAG@
balerd_LDFLAGS += @OCM_LIBDIR_FLAG@
endif
bin_PROGRAMS += balerd

pkginclude_HEADERS = bmem.h bmapper.h butils.h bmlist.h
