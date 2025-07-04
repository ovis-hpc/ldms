include $(top_srcdir)/ldms/rules.mk

noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
EXTRA_DIST =

noinst_LTLIBRARIES += liblustre_shared.la
liblustre_shared_la_SOURCES = lustre_shared.c lustre_shared.h
liblustre_shared_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la
liblustre_shared_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

liblustre_client_la_SOURCES = \
	lustre_client.c \
	lustre_client_general.c \
	lustre_client.h \
	lustre_client_general.h \
	lustre_shared.h
liblustre_client_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la \
	liblustre_shared.la
liblustre_client_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_client_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@
pkglib_LTLIBRARIES += liblustre_client.la
dist_man7_MANS += ldms-sampler_lustre_client.man
EXTRA_DIST += ldms-sampler_lustre_client.rst

liblustre_mdt_la_SOURCES = \
	lustre_mdt.c \
	lustre_mdt_general.c \
	lustre_mdt_job_stats.c \
	lustre_mdt.h \
	lustre_mdt_general.h \
	lustre_mdt_job_stats.h \
	lustre_shared.h
liblustre_mdt_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	liblustre_shared.la
liblustre_mdt_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_mdt_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@
pkglib_LTLIBRARIES += liblustre_mdt.la
dist_man7_MANS += ldms-sampler_lustre_mdt.man
EXTRA_DIST += ldms-sampler_lustre_mdt.rst

liblustre_ost_la_SOURCES = \
	lustre_ost.c \
	lustre_ost_general.c \
	lustre_ost_job_stats.c \
	lustre_ost.h \
	lustre_ost_general.h \
	lustre_ost_job_stats.h \
	lustre_shared.h
liblustre_ost_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	liblustre_shared.la
liblustre_ost_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'ldmsd_plugin_interface' \
        -version-info 1:0:0
liblustre_ost_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@
pkglib_LTLIBRARIES += liblustre_ost.la
dist_man7_MANS += ldms-sampler_lustre_ost.man
EXTRA_DIST += ldms-sampler_lustre_ost.rst

CLEANFILES = $(dist_man7_MANS)
