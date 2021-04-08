libibmad_la_SOURCES = \
        ibmad.c
libibmad_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/ldms/src/sampler/libjobid_helper.la \
	$(LTLIBIBMAD) $(LTLIBIBUMAD)
libibmad_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin'
libibmad_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@ \
	-I/usr/include/infiniband

pkglib_LTLIBRARIES = libibmad.la

dist_man7_MANS = Plugin_ibmad.man
