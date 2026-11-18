include $(top_srcdir)/ldms/rules.mk

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ @AMDSMI_INCS@
AM_LDFLAGS = @OVIS_LIB_ABS@

pkglib_LTLIBRARIES = libamdgpu_sampler.la
libamdgpu_sampler_la_SOURCES = amdgpu_sampler.c amdgpu_sampler.h
libamdgpu_sampler_la_CFLAGS  = $(AM_CFLAGS) @AMDSMI_INCS@
libamdgpu_sampler_la_LDFLAGS = $(AM_LDFLAGS)
libamdgpu_sampler_la_LIBADD  = \
        @LTLIBAMDSMI@ \
        $(top_builddir)/ldms/src/sampler/libsampler_base.la \
        $(top_builddir)/ldms/src/ldmsd/libldmsd_plugattr.la \
        $(top_builddir)/ldms/src/core/libldms.la \
        @LDFLAGS_GETTIME@ \
        $(top_builddir)/lib/src/ovis_util/libovis_util.la \
        $(top_builddir)/lib/src/coll/libcoll.la

# Schema name utility
bin_PROGRAMS = ldms_amdgpu_schema_name
ldms_amdgpu_schema_name_SOURCES = amdgpu_sampler.c
ldms_amdgpu_schema_name_CFLAGS = $(AM_CFLAGS) @AMDSMI_INCS@ -DMAIN
ldms_amdgpu_schema_name_LDFLAGS = $(AM_LDFLAGS)
ldms_amdgpu_schema_name_LDADD = \
        @LIBAMD_SMI@ \
        $(top_builddir)/lib/src/ovis_util/libovis_util.la \
        $(top_builddir)/lib/src/coll/libcoll.la \
        $(top_builddir)/lib/src/ovis_log/libovis_log.la

dist_man7_MANS = ldms-sampler_amdgpu_sampler.man
dist_man1_MANS = ldms_amdgpu_schema_name.man

CLEANFILES = $(dist_man1_MANS) $(dist_man7_MANS)

EXTRA_DIST = ldms_amdgpu_schema_name.rst ldms-sampler_amdgpu_sampler.rst
