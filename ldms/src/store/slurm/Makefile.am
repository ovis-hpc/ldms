include $(top_srcdir)/ldms/rules.mk


# SUBDIRS = flxs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

AM_CFLAGS =  $(DBGFLAGS) -I$(srcdir)/../../sampler/slurm
STORE_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
	       $(top_builddir)/lib/src/coll/libcoll.la \
	       $(top_builddir)/lib/src/ovis_util/libovis_util.la

if ENABLE_SOS
libstore_slurm_la_SOURCES = store_slurm.c
libstore_slurm_la_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
libstore_slurm_la_LIBADD = @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@ $(STORE_LIBADD) -lsos
pkglib_LTLIBRARIES += libstore_slurm.la
dist_man7_MANS += ldms-store_slurm.man
endif

CLEANFILES = $(dist_man7_MANS)
