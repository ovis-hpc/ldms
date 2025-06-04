include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
dist_man1_MANS =

AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ \
	      -I/usr/include/libspl \
	      -I/usr/include/libzfs \
	      -Wall

AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libzfs_leafvdevs_la_SOURCES = zfs_leafvdevs.c
libzfs_leafvdevs_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libzfs_leafvdevs.la
dist_man7_MANS = ldms-sampler_zfs_leafvdevs.man

CLEANFILES = $(dist_man7_MANS)
