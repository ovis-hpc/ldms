ACLOCAL_AMFLAGS = -I m4
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
bin_PROGRAMS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =  scripts/ldms-gen-lidfile.sh  scripts/ldms-ibnet-sampler-gen scripts/ldms-get-opa-network.sh

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

libibnet_la_SOURCES = ibnet_plugin.c ibnet_data.c ibnet_data.h
libibnet_la_LIBADD = $(COMMON_LIBADD) $(LTLIBIBMAD) $(LTLIBIBUMAD) $(LTLIBOSMCOMP)
libibnet_la_LDFLAGS = $(AM_LDFLAGS) -pthread
libibnet_la_CFLAGS = $(AM_CFLAGS) -I/usr/include/infiniband
bin_SCRIPTS += scripts/ldms-ibnet-sampler-gen
bin_SCRIPTS += scripts/ldms-get-opa-network.sh
pkglib_LTLIBRARIES += libibnet.la

bin_PROGRAMS += ldms_ibnet_schema_name
dist_man7_MANS += Plugin_ibnet.man
dist_man1_MANS += ldms_ibnet_schema_name.man ldms-ibnet-sampler-gen.man

ldms_ibnet_schema_name_SOURCES = ibnet_data.c
ldms_ibnet_schema_name_LDADD = $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
                $(top_builddir)/ldms/src/core/libldms.la \
                @LDFLAGS_GETTIME@ \
                $(top_builddir)/lib/src/ovis_util/libovis_util.la \
                $(top_builddir)/lib/src/coll/libcoll.la
ldms_ibnet_schema_name_LDFLAGS = $(AM_LDFLAGS) -lldms -lovis_util  -pthread
ldms_ibnet_schema_name_CFLAGS = $(AM_CFLAGS) -DMAIN -I/usr/include/infiniband

