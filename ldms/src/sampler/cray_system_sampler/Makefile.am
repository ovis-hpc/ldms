pkglib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_CRAY_SYSTEM_SAMPLER
CRAY_CFLAGS_ = $(AM_CFLAGS) @RCA_INCDIR_FLAG@ @KRCA_INCDIR_FLAG@ \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@ \
                        -I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS)  @RCA_LIB64DIR_FLAG@ @RCA_LIBDIR_FLAG@  \
			@CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
			-lrca

CRAY_SOURCES_ = cray_sampler_base.h cray_sampler_base.c \
	rca_metrics.h rca_metrics.c\
	lustre_metrics.h lustre_metrics.c \
	general_metrics.h general_metrics.c

CRAY_LIBADD_ = $(COMMON_LIBADD) \
                       ../lustre/liblustre_sampler.la

#how do I do an or???
if ENABLE_GEMINI_GPCD
librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
pkglib_LTLIBRARIES += librtr_util.la
else
if ENABLE_GEMINI_GPCDR
librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
pkglib_LTLIBRARIES += librtr_util.la
endif
endif

if ENABLE_GEMINI_GPCD
GPCD_UTIL_CFLAGS_= $(CRAY_CFLAGS_) @GPCD_INCDIR_FLAG@
GPCD_UTIL_LDFLAGS_ =  $(CRAY_LD_FLAGS) @GPCD_LIB64DIR_FLAG@ @GPCD_LIBDIR_FLAG@ -lgpcd

libgpcd_util_la_SOURCES = gpcd_util.c gpcd_util.h gemini.h
libgpcd_util_la_CFLAGS = $(GPCD_UTIL_CFLAGS_)
libgpcd_util_la_LDFLAGS = $(GPCD_UTIL_LDFLAGS_)
pkglib_LTLIBRARIES += libgpcd_util.la

libcray_gemini_d_sampler_la_CFLAGS = $(GPCD_UTIL_CFLAGS_)
libcray_gemini_d_sampler_la_LDFLAGS = $(GPCD_UTIL_LDFLAGS_)
libcray_gemini_d_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_gemini_d_sampler.c \
	gpcd_util.h rtr_util.h\
	gemini.h \
	gemini_metrics_gpcd.c gemini_metrics_gpcd.h
libcray_gemini_d_sampler_la_LIBADD = $(CRAY_LIBADD_) \
	./libgpcd_util.la ./librtr_util.la
pkglib_LTLIBRARIES += libcray_gemini_d_sampler.la
endif

if ENABLE_GEMINI_GPCDR
libcray_gemini_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_gemini_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_gemini_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_gemini_r_sampler.c \
	rtr_util.h \
	gemini_metrics_gpcdr.c gemini_metrics_gpcdr.h
libcray_gemini_r_sampler_la_LIBADD = $(CRAY_LIBADD_) \
	./librtr_util.la
pkglib_LTLIBRARIES += libcray_gemini_r_sampler.la
endif

if ENABLE_ARIES_GPCDR
libcray_aries_r_sampler_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_aries_r_sampler_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_aries_r_sampler_la_SOURCES = $(CRAY_SOURCES_) \
	cray_aries_r_sampler.c \
	aries_metrics_gpcdr.c aries_metrics_gpcdr.h
libcray_gemini_r_sampler_la_LIBADD = $(CRAY_LIBADD_)
pkglib_LTLIBRARIES += libcray_aries_r_sampler.la
endif

endif

