include $(top_srcdir)/ldms/rules.mk


lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la

if ENABLE_CRAY_SYSTEM_SAMPLER

# first build dvs sampler
libcray_dvs_sampler_la_SOURCES = dvs_sampler.c
libcray_dvs_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libcray_dvs_sampler.la
dist_man7_MANS += ldms-sampler_cray_dvs.man

# then build cray_system_sampler



CRAY_CFLAGS_ = $(AM_CFLAGS) \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@ \
			-I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS) \
			@CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
			@CRAY_HSS_DEVEL_LIB64DIR_FLAG@

CRAY_SOURCES_ = cray_sampler_base.h cray_sampler_base.c \
	rca_metrics.h rca_metrics.c \
	general_metrics.h general_metrics.c
CRAY_LIBADD_ = $(COMMON_LIBADD)

if ENABLE_LUSTRE
CRAY_SOURCES_ += lustre_metrics.h lustre_metrics.c
CRAY_LIBADD_ += $(top_builddir)/ldms/src/sampler/lustre/liblustre_sampler.la
endif

if ENABLE_GEMINI_GPCDR
librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += librtr_util.la
endif

if ENABLE_GEMINI_GPCDR
if ENABLE_CRAY_NVIDIA
libcray_gemini_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_) @CRAY_NVIDIA_INC_INCDIR_FLAG@ -I$(BASE_INC)
libcray_gemini_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_gemini_r_sampler.c \
	rtr_util.h \
	gemini_metrics_gpcdr.c gemini_metrics_gpcdr.h \
	nvidia_metrics.c nvidia_metrics.h
libcray_gemini_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_) -ldl
else
libcray_gemini_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_gemini_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_gemini_r_sampler.c \
	rtr_util.h \
	gemini_metrics_gpcdr.c gemini_metrics_gpcdr.h
libcray_gemini_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
endif
libcray_gemini_r_sampler_la_LIBADD = $(CRAY_LIBADD_) \
	./librtr_util.la
pkglib_LTLIBRARIES += libcray_gemini_r_sampler.la
dist_man7_MANS += ldms-sampler_cray_system_variants.man
endif

if ENABLE_ARIES_GPCDR
if ENABLE_CRAY_NVIDIA
libcray_aries_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_) @CRAY_NVIDIA_INC_INCDIR_FLAG@
libcray_aries_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_aries_r_sampler.c \
	aries_metrics_gpcdr.c aries_metrics_gpcdr.h \
	nvidia_metrics.c nvidia_metrics.h
libcray_aries_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_) -ldl
else
libcray_aries_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_aries_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_aries_r_sampler.c \
	aries_metrics_gpcdr.c aries_metrics_gpcdr.h
libcray_aries_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
endif
libcray_aries_r_sampler_la_LIBADD = $(CRAY_LIBADD_)
pkglib_LTLIBRARIES += libcray_aries_r_sampler.la
dist_man7_MANS += ldms-sampler_cray_system_variants.man
endif

endif

CLEANFILES = $(dist_man7_MANS)
