libslingshot_info_la_SOURCES = \
        slingshot_info.c
libslingshot_info_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(LTLIBCXI)
libslingshot_info_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin'
libslingshot_info_la_CPPFLAGS = @OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = libslingshot_info.la

dist_man7_MANS = Plugin_slingshot_info.man
