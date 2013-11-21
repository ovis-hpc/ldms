lib_LTLIBRARIES =

CORE = ../../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@

if ENABLE_LUSTRE

CRAY_CFLAGS_ = $(AM_CFLAGS) -I@RCA_INCDIR@ -I@KRCA_INCDIR@ \
			-I@CRAY_HSS_DEVEL_INCDIR@ \
                        -I@CRAY_HSS_DEVEL_INCDIR@/rsms
CRAY_LDFLAGS_ = $(AM_LDFLAGS)  -L@RCA_LIB64DIR@ -L@RCA_LIBDIR@  \
			-L@CRAY_HSS_DEVEL_LIBDIR@ \
			-lrca

librtr_util_la_SOURCES = rtr_util.c rtr_util.h gemini.h
librtr_util_la_CFLAGS = $(CRAY_CFLAGS_)
librtr_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += librtr_util.la

CRAY_SOURCES_ = cray_system_sampler.c \
	gemini_metrics.h \
	rca_metrics.h rca_metrics.c\
	general_metrics.c general_metrics.h \
	rtr_util.c rtr_util.h
CRAY_LIBADD_ = $(COMMON_LIBADD) \
	               ../lustre/libstr_map.la \
                       ../lustre/liblustre_sampler.la \
			./librtr_util.la

if ENABLE_GPCDR
libcray_system_sampler_r_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_system_sampler_r_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_system_sampler_r_la_SOURCES = $(CRAY_SOURCES_) \
	gemini_metrics_gpcdr.c gemini_metrics_gpcdr.h
libcray_system_sampler_r_la_LIBADD = $(CRAY_LIBADD_)
lib_LTLIBRARIES += libcray_system_sampler_r.la

else

CRAY_CFLAGS_ += -I@GPCD_INCDIR@ -I/home/gentile/junk
CRAY_LDFLAGS_ += -L@GPCD_LIB64DIR@ -L@GPCD_LIBDIR@ -lgpcd

libgpcd_util_la_SOURCES = gpcd_util.c gpcd_util.h gemini.h
libgpcd_util_la_CFLAGS = $(CRAY_CFLAGS_)
libgpcd_util_la_LDFLAGS = $(CRAY_LDFLAGS_)
lib_LTLIBRARIES += libgpcd_util.la

libcray_system_sampler_d_la_CFLAGS = $(CRAY_CFLAGS_)
libcray_system_sampler_d_la_LDFLAGS = $(CRAY_LDFLAGS_)
libcray_system_sampler_d_la_SOURCES = $(CRAY_SOURCES_) \
	gpcd_util.c gpcd_util.h \
	gemini_metrics_gpcd.c gemini_metrics_gpcd.h
libcray_system_sampler_d_la_LIBADD = $(CRAY_LIBADD_) ./libgpcd_util.la
lib_LTLIBRARIES += libcray_system_sampler_d.la
endif
endif
