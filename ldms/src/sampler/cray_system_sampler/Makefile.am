lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_CRAY_SYSTEM_SAMPLER

CRAY_CFLAGS_ = $(AM_CFLAGS) @RCA_INCDIR_FLAG@ @KRCA_INCDIR_FLAG@ \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@ \
                        -I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS) @RCA_LIBDIR_FLAG@ @KRCA_LIBDIR_FLAG@  \
			@CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
			-lrca

CRAY_SOURCES_ = cray_sampler_base.h cray_sampler_base.c \
	rca_metrics.h rca_metrics.c \
	general_metrics.h general_metrics.c
CRAY_LIBADD_ = $(COMMON_LIBADD)

if ENABLE_LUSTRE
CRAY_SOURCES_ += lustre_metrics.h lustre_metrics.c
CRAY_LIBADD_ += ../lustre/liblustre_sampler.la
endif

if ENABLE_GEMINI_GPCDR
librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += librtr_util.la
endif

if ENABLE_GEMINI_GPCDR
if ENABLE_CRAY_NVIDIA
libcray_gemini_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_) @CRAY_NVIDIA_INC_INCDIR_FLAG@ -I /usr/include/nvidia/gdk/
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
lib_LTLIBRARIES += libcray_gemini_r_sampler.la
endif

if ENABLE_ARIES_GPCDR
if ENABLE_CRAY_NVIDIA
libcray_aries_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_) @CRAY_NVIDIA_INC_INCDIR_FLAG@ -I /usr/include/nvidia/gdk/
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
lib_LTLIBRARIES += libcray_aries_r_sampler.la
endif

endif
