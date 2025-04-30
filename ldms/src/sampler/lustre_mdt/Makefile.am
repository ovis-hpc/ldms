include $(top_srcdir)/ldms/rules.mk


liblustre_mdt_la_SOURCES = \
	lustre_mdt.c \
	lustre_mdt_general.c \
	lustre_mdt_job_stats.c \
	lustre_mdt.h \
	lustre_mdt_general.h \
	lustre_mdt_job_stats.h
liblustre_mdt_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la

liblustre_mdt_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_mdt_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = liblustre_mdt.la

dist_man7_MANS = ldms-sampler_lustre_mdt.man
