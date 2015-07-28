ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
lib_LTLIBRARIES =
CORE = ../core

AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = $(CORE)/libldms.la -lrt -lovis_util

if ENABLE_KGNILND
libkgnilnd_la_SOURCES = kgnilnd.c
libkgnilnd_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libkgnilnd.la
endif

if ENABLE_LUSTRE
SUBDIRS += lustre
endif

if ENABLE_HADOOP
SUBDIRS += hadoop
endif

if ENABLE_ARRAY_EXAMPLE
libarray_example_la_SOURCES = array_example.c
libarray_example_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libarray_example.la
endif

if ENABLE_MEMINFO
libmeminfo_la_SOURCES = meminfo.c
libmeminfo_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libmeminfo.la
endif

if ENABLE_SWITCHX
libswitchx_la_SOURCES = switchx.c
libswitchx_la_LIBADD = $(COMMON_LIBADD) \
	$(SWITCHX_LIBDIR)/libsxapi.a \
	$(SWITCHX_LIBDIR)/libsxutils.a \
	$(SWITCHX_LIBDIR)/libsxdemadparser.a \
	$(SWITCHX_LIBDIR)/libsxcomp.a \
	$(SWITCHX_LIBDIR)/libsxdev.a \
	$(SWITCHX_LIBDIR)/libcommon.a \
	$(SWITCHX_LIBDIR)/libsxlog.a \
	$(SWITCHX_LIBDIR)/libibmad.a \
	$(SWITCHX_LIBDIR)/libibumad.a \
	$(SWITCHX_LIBDIR)/libosmcomp.a
libswitchx_la_CFLAGS = $(AM_CFLAGS) \
	-I$(SWITCHX_LIBDIR)/../usr/include
lib_LTLIBRARIES += libswitchx.la
endif

if ENABLE_PERFEVENT
libperfevent_la_SOURCES = perfevent.c
libperfevent_la_LIBADD = $(COMMON_LIBADD) -lm
lib_LTLIBRARIES += libperfevent.la
endif

if ENABLE_PROCDISKSTATS
libprocdiskstats_la_SOURCES = procdiskstats.c
libprocdiskstats_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocdiskstats.la
endif

if ENABLE_PROCINTERRUPTS
libprocinterrupts_la_SOURCES = procinterrupts.c
libprocinterrupts_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocinterrupts.la
endif

if ENABLE_PROCNETDEV
libprocnetdev_la_SOURCES = procnetdev.c
libprocnetdev_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocnetdev.la
endif

if ENABLE_PROCNFS
libprocnfs_la_SOURCES = procnfs.c
libprocnfs_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocnfs.la
endif

if ENABLE_PROCSENSORS
libprocsensors_la_SOURCES = procsensors.c
libprocsensors_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocsensors.la
endif

if ENABLE_PROCSTATUTIL
libprocstatutil_la_SOURCES = procstatutil.c
libprocstatutil_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libprocstatutil.la
endif

if ENABLE_GLIB
if ENABLE_SEDC
libsedc_la_SOURCES = sedc.c
libsedc_la_CFLAGS = $(AM_CFLAGS) $(GLIB20_CFLAGS)
libsedc_la_LIBADD = $(COMMON_LIBADD) $(GLIB20_LIBS)
lib_LTLIBRARIES += libsedc.la
endif
endif

if ENABLE_SYSCLASSIB
libsysclassib_la_SOURCES = sysclassib.c
libsysclassib_la_LIBADD = $(COMMON_LIBADD) -libmad -libumad
lib_LTLIBRARIES += libsysclassib.la
endif

if ENABLE_VMSTAT
libvmstat_la_SOURCES = vmstat.c
libvmstat_la_LIBADD = $(COMMON_LIBADD)
lib_LTLIBRARIES += libvmstat.la
endif

if ENABLE_NCSA_UNIFIED
if ENABLE_RCA
if ENABLE_KRCA
if ENABLE_GPCD
if ENABLE_CRAY_HSS_DEVEL
if ENABLE_LUSTRE
# NCSA_UNIFIED requires LUSTRE AND GPCD-related

CRAY_CFLAGS_ = $(AM_CFLAGS) @GPCD_INCDIR_FLAG@ @RCA_INCDIR_FLAG@ \
			     @KRCA_INCDIR_FLAG@ @CRAY_HSS_DEVEL_INCDIR_FLAG@ \
                             -I@CRAY_HSS_DEVEL_INCDIR@/rsms

CRAY_LDFLAGS_ = $(AM_LDFLAGS) @GPCD_LIBDIR_FLAG@ @RCA_LIBDIR_FLAG@ \
			      -lgpcd -lrca

libgem_link_perf_util_la_SOURCES = gem_link_perf_util.c gem_link_perf_util.h
libgem_link_perf_util_la_CFLAGS = $(CRAY_CFLAGS_)
libgem_link_perf_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += libgem_link_perf_util.la


libncsa_unified_la_SOURCES = ncsa_unified.c
libncsa_unified_la_CFLAGS = $(CRAY_CFLAGS_)
libncsa_unified_la_LDFLAGS = $(CRAY_LDFLAGS_)
libncsa_unified_la_LIBADD = $(COMMON_LIBADD) \
			    ./lustre/libstr_map.la \
			    ./lustre/liblustre_sampler.la \
			    ./libgem_link_perf_util.la
lib_LTLIBRARIES += libncsa_unified.la
endif
endif
endif
endif
endif
endif

if ENABLE_ATASMART
libsampler_atasmart_la_SOURCES = sampler_atasmart.c
libsampler_atasmart_la_LIBADD = $(COMMON_LIBADD) -latasmart
lib_LTLIBRARIES += libsampler_atasmart.la
endif

if ENABLE_GENERIC_SAMPLER
libgeneric_sampler_la_SOURCES = generic_sampler.c
libgeneric_sampler_la_LIBADD = $(COMMON_LIBADD) -lcoll
lib_LTLIBRARIES += libgeneric_sampler.la
endif

#if ENABLE_GEM_LINK_PERF
#libgem_link_perf_la_SOURCES = gem_link_perf.c
#libgem_link_perf_la_CFLAGS = $(AM_CFLAGS) -I@GPCD_INCDIR@ -I@RCA_INCDIR@ \
#			     -I@KRCA_INCDIR@
#libgem_link_perf_la_LDFLAGS = $(AM_LDFLAGS) -L@GPCD_LIB64DIR@ -L@RCA_LIB64DIR@ \
#			      -L@GPCD_LIBDIR@ -L@RCA_LIBDIR@ \
#			      -lgpcd -lrca
#libgem_link_perf_la_LIBADD = $(COMMON_LIBADD)
#lib_LTLIBRARIES += libgem_link_perf.la
#endif




