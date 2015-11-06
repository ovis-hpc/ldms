lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

# libgpcd and headers are *NOT* part of LDMS and should be elsewhere on your system
if ENABLE_ARIES_MMR
# ARIES_LIBGPCD_INCDIR=@ARIES_LIBGPCD_INCDIR@
# ARIES_LIBGPCD_LIBDIR=@ARIES_LIBGPCD_LIBDIR@

libaries_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_mmr_la_SOURCES = aries_mmr.c
libaries_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd
libaries_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
                                  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_mmr.la

libaries_nic_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_nic_mmr_la_SOURCES = aries_nic_mmr.c
libaries_nic_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd
libaries_nic_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
                                  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_nic_mmr.la


libaries_rtr_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_rtr_mmr_la_SOURCES = aries_rtr_mmr.c
libaries_rtr_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcd
libaries_rtr_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
                                  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_rtr_mmr.la



endif
