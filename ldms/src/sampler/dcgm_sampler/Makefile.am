bin_PROGRAMS = ldms-dcgm-list-fields

libdcgm_sampler_la_SOURCES = \
	dcgm_sampler.c

libdcgm_sampler_la_LIBADD = \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la \
	-ldcgm

libdcgm_sampler_la_LDFLAGS = \
	-no-undefined \
	-export-symbols-regex 'get_plugin' \
	-version-info 1:0:0
libdcgm_sampler_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = libdcgm_sampler.la

dist_man7_MANS = Plugin_dcgm_sampler.man

dist_noinst_SCRIPTS = gen-ldms-dcgm-list-fields

ldms-dcgm-list-fields.c: $(srcdir)/gen-ldms-dcgm-list-fields
	$(srcdir)/gen-ldms-dcgm-list-fields > ldms-dcgm-list-fields.c

ldms_dcgm_list_fields_SOURCES = ldms-dcgm-list-fields.c
ldms_dcgm_list_fields_CPPFLAGS = @OVIS_INCLUDE_ABS@
ldms_dcgm_list_fields_LDADD = -ldcgm

clean-local::
	$(RM) $(builddir)/ldms_dcgm_list_fields.c
