include $(top_srcdir)/ldms/rules.mk


liblustre_ost_la_SOURCES = \
	lustre_ost.c \
	lustre_ost_general.c \
	lustre_ost_job_stats.c \
	lustre_ost.h \
	lustre_ost_general.h \
	lustre_ost_job_stats.h
liblustre_ost_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la

liblustre_ost_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_ost_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = liblustre_ost.la

dist_man7_MANS = ldms-sampler_lustre_ost.man
