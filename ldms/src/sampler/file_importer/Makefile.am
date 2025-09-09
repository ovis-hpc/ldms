include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_FILE_IMPORTER
libfile_importer_la_SOURCES = file_importer.c
libfile_importer_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libfile_importer.la
dist_man7_MANS += ldms-sampler_file_importer.man
endif

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_file_importer.rst
