lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@

if ENABLE_LUSTRE
if ENABLE_RCA
if ENABLE_KRCA
if ENABLE_GPCD
if ENABLE_CRAY_HSS_DEVEL

libgem_link_perf_util_la_SOURCES = gem_link_perf_util.c gem_link_perf_util.h
libgem_link_perf_util_la_CFLAGS = $(CRAY_CFLAGS_)
libgem_link_perf_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += libgem_link_perf_util.la

CRAY_CFLAGS_ = $(AM_CFLAGS) -I@GPCD_INCDIR@ -I@RCA_INCDIR@ \
                             -I@KRCA_INCDIR@ -I@CRAY_HSS_DEVEL_INCDIR@ \
                             -I@CRAY_HSS_DEVEL_INCDIR@/rsms

CRAY_LDFLAGS_ = $(AM_LDFLAGS) -L@GPCD_LIB64DIR@ -L@RCA_LIB64DIR@ \
                              -L@GPCD_LIBDIR@ -L@RCA_LIBDIR@ \
                              -lgpcd -lrca

libcray_system_sampler_la_SOURCES = cray_system_sampler.c \
	gemini_metrics.c gemini_metrics.h \
	general_metrics.c general_metrics.h
libcray_system_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_system_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_system_sampler_la_LIBADD = $(COMMON_LIBADD) \
                            ../lustre/libstr_map.la \
                            ../lustre/liblustre_sampler.la \
                            ./libgem_link_perf_util.la
lib_LTLIBRARIES += libcray_system_sampler.la
endif
endif
endif
endif
endif