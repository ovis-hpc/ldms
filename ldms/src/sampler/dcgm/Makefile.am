libdcgm_la_SOURCES = \
        dcgm.c
libdcgm_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/ldms/src/sampler/libjobid_helper.la \
	-ldcgm
libdcgm_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin' \
        -version-info 1:0:0
libdcgm_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = libdcgm.la

dist_man7_MANS = Plugin_dcgm.man
