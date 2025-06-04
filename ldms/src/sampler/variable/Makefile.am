include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_VARSET
libvariable_la_SOURCES = variable.c
libvariable_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libvariable.la
dist_man7_MANS += ldms-sampler_variable.man
endif

CLEANFILES = $(dist_man7_MANS)
