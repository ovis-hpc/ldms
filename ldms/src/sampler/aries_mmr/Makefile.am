SUBDIRS = aries_mmr_set_configs
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_ARIES_MMR
# ARIES_LIBGPCD_INCDIR=@ARIES_LIBGPCD_INCDIR@
# ARIES_LIBGPCD_LIBDIR=@ARIES_LIBGPCD_LIBDIR@

libaries_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_mmr_la_SOURCES = aries_mmr.c
libaries_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@
libaries_mmr_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_mmr.la

libaries_nic_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_nic_mmr_la_SOURCES = aries_nic_mmr.c
libaries_nic_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd
libaries_nic_mmr_la_LIBADD = $(COMMON_LIBADD) -lgpcd
pkglib_LTLIBRARIES += libaries_nic_mmr.la


libaries_rtr_mmr_la_CFLAGS = -I$(ARIES_LIBGPCD_INCDIR)
libaries_rtr_mmr_la_SOURCES = aries_rtr_mmr.c
libaries_rtr_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@
libaries_rtr_mmr_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libaries_rtr_mmr.la

endif


# Can make linkstatus w/o craysystem sampler and w/o aries_mmr and vice versa.
# Decided when we will switch to the new naming convention where cray specific items are prefixed with cray
# Putting it here for arrangement purposes
if ENABLE_ARIES_LINKSTATUS
libaries_linkstatus_la_SOURCES = aries_linkstatus.c
libaries_linkstatus_la_LIBADD = $(COMMON_LIBADD) 
pkglib_LTLIBRARIES += libaries_linkstatus.la
endif
