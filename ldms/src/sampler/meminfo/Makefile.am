pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =


CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

BASE_LIBADD = ../libldmsd_sampler_base.la
COMMON_LIBADD = $(CORE)/libldms.la \
            @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_MEMINFO
libmeminfo_la_SOURCES = meminfo.c 
libmeminfo_la_LIBADD =  $(BASE_LIBADD) $(COMMON_LIBADD)
libmeminfo_la_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/$(SAMPLER)
pkglib_LTLIBRARIES += libmeminfo.la
dist_man7_MANS += Plugin_meminfo.man
endif


