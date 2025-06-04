include $(top_srcdir)/ldms/rules.mk


SUBDIRS = aries_mmr_set_configs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
bin_PROGRAMS = check_mmr_configurable

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_ARIES_MMR

libaries_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_mmr_la_SOURCES = aries_mmr.c
libaries_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@
libaries_mmr_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_mmr.la
dist_man7_MANS += ldms-sampler_aries_mmr.man

libaries_nic_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_nic_mmr_la_SOURCES = aries_nic_mmr.c
libaries_nic_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@
libaries_nic_mmr_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_nic_mmr.la


libaries_rtr_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_rtr_mmr_la_SOURCES = aries_rtr_mmr.c
libaries_rtr_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@
libaries_rtr_mmr_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_rtr_mmr.la


libaries_mmr_configurable_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR) -I$(BASE_INC)
libaries_mmr_configurable_la_SOURCES = aries_mmr_configurable.c
libaries_mmr_configurable_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd
libaries_mmr_configurable_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_mmr_configurable.la
dist_man7_MANS += ldms-sampler_aries_mmr_configurable.man

check_mmr_configurable_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
check_mmr_configurable_SOURCES = check_mmr_configurable.c
check_mmr_configurable_LDFLAGS = -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd


endif


# Can make linkstatus w/o craysystem sampler and w/o aries_mmr and vice versa.
# Decided when we will switch to the new naming convention where cray specific items are prefixed with cray
# Putting it here for arrangement purposes
if ENABLE_ARIES_LINKSTATUS
libaries_linkstatus_la_SOURCES = aries_linkstatus.c
libaries_linkstatus_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libaries_linkstatus.la
dist_man7_MANS += ldms-sampler_aries_linkstatus.man

endif

CLEANFILES = $(dist_man7_MANS)
