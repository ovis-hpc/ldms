include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

libmsr_interlagos_la_SOURCES = msr_interlagos.c
libmsr_interlagos_la_LIBADD = $(COMMON_LIBADD) -lpthread
pkglib_LTLIBRARIES += libmsr_interlagos.la
dist_man7_MANS += ldms-sampler_msr_interlagos.man

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_msr_interlagos.rst
