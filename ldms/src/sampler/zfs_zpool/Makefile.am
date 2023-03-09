pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ \
	      -I/usr/include/libzfs \
	      -I/usr/include/libspl \
	      -I/usr/include/libzfs/sys
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_PROCNETDEV
libzfs_zpool_la_SOURCES = zfs_zpool.c \
			  zpool_ldms_helper.c

libzfs_zpool_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libzfs_zpool.la
endif
