ACLOCAL_AMFLAGS = -I m4
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
bin_PROGRAMS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

rdclib = -lrdc_bootstrap

librdc_sampler_la_SOURCES = rdc_plugin.c rdcinfo.c rdcinfo.h
librdc_sampler_la_CFLAGS  = $(AM_CFLAGS) $(RDC_CFLAGS)
librdc_sampler_la_LDFLAGS = $(AM_LDFLAGS) $(RDC_LIBS)
librdc_sampler_la_LIBADD  = $(COMMON_LIBADD) $(rdclib)
pkglib_LTLIBRARIES += librdc_sampler.la


bin_PROGRAMS += ldms_rdc_schema_name
dist_man7_MANS += Plugin_rdc_sampler.man
dist_man1_MANS += ldms_rdc_schema_name.man

ldms_rdc_schema_name_SOURCES = rdcinfo.c
ldms_rdc_schema_name_CFLAGS = $(AM_CFLAGS) -DMAIN $(RDC_CFLAGS)
ldms_rdc_schema_name_LDFLAGS = $(AM_LDFLAGS) $(RDC_LIBS) $(rdclib)
ldms_rdc_schema_name_LDADD = \
	$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
	$(top_builddir)/lib/src/coll/libcoll.la

