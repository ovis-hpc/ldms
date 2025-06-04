include $(top_srcdir)/ldms/rules.mk


ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

pkglib_LTLIBRARIES = librdc_sampler.la
librdc_sampler_la_SOURCES = rdc_plugin.c rdcinfo.c rdcinfo.h
librdc_sampler_la_CFLAGS  = $(AM_CFLAGS)
librdc_sampler_la_LDFLAGS = $(AM_LDFLAGS)
librdc_sampler_la_LIBADD  = \
	$(LTLIBRDC_BOOTSTRAP) \
	$(LTLIBRDC_RAS) \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	@LDFLAGS_GETTIME@ \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
	$(top_builddir)/lib/src/coll/libcoll.la


bin_PROGRAMS = ldms_rdc_schema_name
ldms_rdc_schema_name_SOURCES = rdcinfo.c
ldms_rdc_schema_name_CFLAGS = $(AM_CFLAGS) -DMAIN
ldms_rdc_schema_name_LDFLAGS = $(AM_LDFLAG)
ldms_rdc_schema_name_LDADD = \
	$(LIBRDC_BOOTSTRAP) \
	$(LIBRDC_RAS) \
	$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
	$(top_builddir)/lib/src/coll/libcoll.la

dist_man7_MANS = ldms-sampler_rdc_sampler.man
dist_man1_MANS = ldms_rdc_schema_name.man

CLEANFILES = $(dist_man1_MANS) $(dist_man7_MANS)
