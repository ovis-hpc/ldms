pkglib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

if ENABLE_LUSTRE

CRAY_CFLAGS_ = $(AM_CFLAGS) @RCA_INCDIR_FLAG@ @KRCA_INCDIR_FLAG@ \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@ \
                        -I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS)  @RCA_LIB64DIR_FLAG@ @RCA_LIBDIR_FLAG@  \
			@CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
			-lrca

librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
pkglib_LTLIBRARIES += librtr_util.la

CRAY_SOURCES_ = cray_system_sampler.c \
	gemini_metrics.h \
	rca_metrics.h rca_metrics.c\
	general_metrics.c general_metrics.h \
	rtr_util.c rtr_util.h
CRAY_LIBADD_ = $(COMMON_LIBADD) \
                       ../lustre/liblustre_sampler.la \
			./librtr_util.la

if ENABLE_GPCDR
libcray_system_sampler_r_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_system_sampler_r_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_system_sampler_r_la_SOURCES = $(CRAY_SOURCES_) \
	gemini_metrics_gpcdr.c gemini_metrics_gpcdr.h
libcray_system_sampler_r_la_LIBADD = $(CRAY_LIBADD_)
pkglib_LTLIBRARIES += libcray_system_sampler_r.la

else

CRAY_CFLAGS_ += @GPCD_INCDIR_FLAG@
CRAY_LDFLAGS_ += @GPCD_LIB64DIR_FLAG@ @GPCD_LIBDIR_FLAG@ -lgpcd

libgpcd_util_la_SOURCES = gpcd_util.c gpcd_util.h gemini.h
libgpcd_util_la_CFLAGS = $(CRAY_CFLAGS_)
libgpcd_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
pkglib_LTLIBRARIES += libgpcd_util.la

libcray_system_sampler_d_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_system_sampler_d_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_system_sampler_d_la_SOURCES = $(CRAY_SOURCES_) \
	gpcd_util.c gpcd_util.h \
	gemini_metrics_gpcd.c gemini_metrics_gpcd.h
libcray_system_sampler_d_la_LIBADD = $(CRAY_LIBADD_) ./libgpcd_util.la
pkglib_LTLIBRARIES += libcray_system_sampler_d.la
endif
endif
