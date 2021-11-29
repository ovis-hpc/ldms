libdcgm_sampler_la_SOURCES = \
        dcgm_sampler.c
libdcgm_sampler_la_LIBADD = \
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

# libdcgm is DEPRECATED
#
# This symlink is to maintain backward compatibilty as we rename
# the plugin from "dcgm" to "dcgm_sampler" in 4.3.8. The next time
# that we make a major verion change allowing a break in compatibility,
# this symlink can be removed.
install-exec-hook:
	$(LN_S) libdcgm_sampler.so $(DESTDIR)$(pkglibdir)/libdcgm.so

uninstall-hook:
	rm $(DESTDIR)$(pkglibdir)/libdcgm.so
