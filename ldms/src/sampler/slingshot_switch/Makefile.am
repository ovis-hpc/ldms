include $(top_srcdir)/ldms/rules.mk

libslingshot_switch_la_SOURCES = \
	slingshot_switch.c
libslingshot_switch_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la
libslingshot_switch_la_LDFLAGS = \
	-no-undefined \
	-export-symbols-regex 'ldmsd_plugin_interface'
libslingshot_switch_la_CPPFLAGS = @OVIS_INCLUDE_ABS@

libslingshot_switch_1_la_SOURCES = \
	slingshot_switch_1.c
libslingshot_switch_1_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la
libslingshot_switch_1_la_LDFLAGS = \
	-no-undefined \
	-export-symbols-regex 'ldmsd_plugin_interface'
libslingshot_switch_1_la_CPPFLAGS = @OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = libslingshot_switch.la libslingshot_switch_1.la

dist_man7_MANS = ldms-sampler_slingshot_switch.man

CLEANFILES = $(dist_man7_MANS)
