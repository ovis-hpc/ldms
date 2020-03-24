pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la

if ENABLE_IBM_OCC_SAMPLER
libibm_occ_la_SOURCES = ibm_occ.c ibm_occ.h
libibm_occ_la_LIBADD = $(COMMON_LIBADD)
libibm_occ_la_CFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libibm_occ.la
endif

