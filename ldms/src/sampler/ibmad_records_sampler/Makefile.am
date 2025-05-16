include $(top_srcdir)/ldms/rules.mk


libibmad_records_sampler_la_SOURCES = \
        ibmad_records_sampler.c
libibmad_records_sampler_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(LTLIBIBMAD) $(LTLIBIBUMAD)
libibmad_records_sampler_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface'
libibmad_records_sampler_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@ \
	-I/usr/include/infiniband

pkglib_LTLIBRARIES = libibmad_records_sampler.la

dist_man7_MANS = ldms-sampler_ibmad_records_sampler.man
