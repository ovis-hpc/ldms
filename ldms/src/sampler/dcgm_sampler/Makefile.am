include $(top_srcdir)/ldms/rules.mk


libdcgm_sampler_la_SOURCES = \
        dcgm_sampler.c
libdcgm_sampler_la_LIBADD = \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
        $(top_builddir)/ldms/src/sampler/libjobid_helper.la \
	-ldcgm
libdcgm_sampler_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
libdcgm_sampler_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@ \
	-I/usr/include/datacenter-gpu-manager-4

pkglib_LTLIBRARIES = libdcgm_sampler.la

dist_man7_MANS = ldms-sampler_dcgm_sampler.man

CLEANFILES = $(dist_man7_MANS)

EXTRA_DIST=ldms-sampler_dcgm_sampler.rst
