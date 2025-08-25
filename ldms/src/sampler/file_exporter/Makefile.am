include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_FILE_EXPORTER
libfile_exporter_la_SOURCES = file_exporter.c
libfile_exporter_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libfile_exporter.la
dist_man7_MANS += ldms-sampler_file_exporter.man
endif

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_file_exporter.rst
