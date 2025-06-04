include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD) $(LTLIBIBMAD) $(LTLIBIBUMAD)
pkglib_LTLIBRARIES += libsysclassib.la
dist_man7_MANS += ldms-sampler_sysclassib.man

CLEANFILES = $(dist_man7_MANS)
