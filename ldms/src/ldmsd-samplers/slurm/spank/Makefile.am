pkglib_LTLIBRARIES =
dist_man7_MANS=

libslurm_notifier_la_SOURCES = slurm_notifier.c
libslurm_notifier_la_LIBADD = $(top_builddir)/ldms/src/core/libldms.la \
			      $(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
			      @LDFLAGS_GETTIME@ \
			      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			      $(top_builddir)/lib/src/json/libjson_util.la \
			      $(top_builddir)/lib/src/coll/libcoll.la
libslurm_notifier_la_CFLAGS = $(AM_CFLAGS) \
			      -I$(top_srcdir)/ldms/src/core \
			      -I$(top_srcdir)/ldms/src/ldmsd \
			      -I$(top_builddir) \
			      -I$(top_srcdir)/lib/src $(SLURM_INCDIR_FLAG) \
			      -DSYSCONFDIR='"$(sysconfdir)"'
# libslurm_notifier_la_LDFLAGS = -module
pkglib_LTLIBRARIES += libslurm_notifier.la
