lib_LTLIBRARIES =
pkglib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@
BASE_SRC = ../sampler_base.h ../sampler_base.c
BASE_INC = $(srcdir)/../

# libgpcdlocal and headers are *NOT* part of LDMS and should be elsewhere on your system
# Specific packagings of ldms (cray) may for convenience include source and binaries of libgpcdlocal
# pulled from non-LDMS code repositories.
if ENABLE_ARIES_MMR
# ARIES_LIBGPCD_INCDIR=@ARIES_LIBGPCD_INCDIR@
# ARIES_LIBGPCD_LIBDIR=@ARIES_LIBGPCD_LIBDIR@

libaries_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR) -I$(BASE_INC)
libaries_mmr_la_SOURCES = aries_mmr.c $(BASE_SRC)
libaries_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcdlocal
libaries_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_mmr.la

libaries_nic_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR) -I$(BASE_INC)
libaries_nic_mmr_la_SOURCES = aries_nic_mmr.c $(BASE_SRC)

libaries_nic_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcdlocal
libaries_nic_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_nic_mmr.la


libaries_rtr_mmr_la_CFLAGS = $(AM_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR) -I$(BASE_INC)
libaries_rtr_mmr_la_SOURCES = aries_rtr_mmr.c $(BASE_SRC)
libaries_rtr_mmr_la_LDFLAGS = $(AM_LDFLAGS) -L@ARIES_LIBGPCD_LIBDIR@ -lgpcdlocal
libaries_rtr_mmr_la_LIBADD = $(CORE)/libldms.la -lovis_util \
				  @LDFLAGS_GETTIME@ -lcoll
pkglib_LTLIBRARIES += libaries_rtr_mmr.la

endif


# Can make linkstatus w/o craysystem sampler and w/o aries_mmr and vice versa.
# Switching to the new naming convention where cray specific items are prefixed with cray
# Putting it here for arrangement purposes
if ENABLE_ARIES_LINKSTATUS
libaries_linkstatus_la_SOURCES = aries_linkstatus.c $(BASE_SRC)
libaries_linkstatus_la_CFLAGS = $(AM_CFLAGS) -I$(BASE_INC)
libaries_linkstatus_la_LDFLAGS = $(AM_LDFLAGS)
libaries_linkstatus_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libaries_linkstatus.la
endif
