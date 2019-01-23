include ../common.am

pkglib_LTLIBRARIES = libdvs_sampler.la

libdvs_sampler_la_SOURCES = dvs_sampler.c
libdvs_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
libdvs_sampler_la_LIBADD = $(SAMPLER_LIBADD)
libdvs_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@

CRAY_CFLAGS_ = $(AM_CFLAGS) $(SAMPLER_CFLAGS) \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@ \
			-I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS) $(SAMPLER_LDFLAGS) \
			@CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
			@CRAY_HSS_DEVEL_LIB64DIR_FLAG@
CRAY_SOURCES_ = cray_sampler_base.h cray_sampler_base.c \
	rca_metrics.h rca_metrics.c \
	general_metrics.h general_metrics.c
CRAY_LIBADD_ = $(SAMPLER_LIBADD)

if ENABLE_LUSTRE
CRAY_SOURCES_ += lustre_metrics.h lustre_metrics.c
CRAY_LIBADD_ += ../lustre/liblustre_sampler.la
endif

if ENABLE_CRAY_NVIDIA
CRAY_SOURCES_ += nvidia_metrics.c nvidia_metrics.h
CRAY_CFLAGS_  += @CRAY_NVIDIA_INC_INCDIR_FLAG@
CRAY_LDFLAGS_ += -ldl
endif

if ENABLE_ARIES_GPCDR
libcray_aries_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_aries_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_aries_r_sampler.c aries_metrics_gpcdr.c aries_metrics_gpcdr.h
libcray_aries_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_aries_r_sampler_la_LIBADD = $(CRAY_LIBADD_)
pkglib_LTLIBRARIES += libcray_aries_r_sampler.la
endif
