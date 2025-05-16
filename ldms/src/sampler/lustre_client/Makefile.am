include $(top_srcdir)/ldms/rules.mk


liblustre_client_la_SOURCES = \
        lustre_client.c \
        lustre_client_general.c \
        lustre_client.h \
        lustre_client_general.h
liblustre_client_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la

liblustre_client_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_client_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = liblustre_client.la

dist_man7_MANS = ldms-sampler_lustre_client.man
