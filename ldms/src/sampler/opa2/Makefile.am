include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@


OPA2_LIBADD = $(LTLIBIBMAD) $(LTLIBIBUMAD) -lpthread
libopa2_la_SOURCES = opa2.c
libopa2_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
libopa2_la_LIBADD = $(AM_LDFLAGS) $(COMMON_LIBADD) $(OPA2_LIBADD) $(JOBID_LIBFLAGS)
dist_man7_MANS += ldms-sampler_opa2.man
pkglib_LTLIBRARIES += libopa2.la

CLEANFILES = $(dist_man7_MANS)
