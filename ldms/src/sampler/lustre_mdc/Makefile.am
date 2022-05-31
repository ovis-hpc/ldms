liblustre_mdc_la_SOURCES = \
	lustre_mdc.c \
	lustre_mdc_general.c \
	lustre_mdc.h \
	lustre_mdc_general.h

liblustre_mdc_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la

liblustre_mdc_la_LDFLAGS = \
	-no-undefined \
	-export-symbols-regex 'get_plugin' \
	-version-info 1:0:0
liblustre_mdc_la_CFLAGS = $(AM_CFLAGS)
liblustre_mdc_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = liblustre_mdc.la

dist_man7_MANS = Plugin_lustre_mdc.man
