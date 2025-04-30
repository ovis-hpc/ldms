include $(top_srcdir)/ldms/rules.mk


libibmad_sampler_la_SOURCES = \
        ibmad_sampler.c
libibmad_sampler_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/ldms/src/sampler/libjobid_helper.la \
        $(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(LTLIBIBMAD) $(LTLIBIBUMAD)
libibmad_sampler_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface'
libibmad_sampler_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@ \
	-I/usr/include/infiniband

pkglib_LTLIBRARIES = libibmad_sampler.la

dist_man7_MANS = ldms-sampler_ibmad_sampler.man

# libibmad is DEPRECATED
#
# This symlink is to maintain backward compatibilty as we rename
# the plugin from "ibmad" to "ibmad_sampler" in 4.3.8. The next time
# that we make a major verion change allowing a break in compatibility,
# this symlink can be removed.
install-exec-hook:
	$(LN_S) -f libibmad_sampler.so $(DESTDIR)$(pkglibdir)/libibmad.so

uninstall-hook:
	rm $(DESTDIR)$(pkglibdir)/libibmad.so
