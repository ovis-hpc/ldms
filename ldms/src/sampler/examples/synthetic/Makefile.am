pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
check_PROGRAMS =
dist_man7_MANS =

CORE = ../../../core
SAMPLER= ../../../sampler
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

COMMON_LIBADD = -lsampler_base -lldms \
	    @LDFLAGS_GETTIME@ -lovis_util -lcoll


if ENABLE_SYNTHETIC
libsynthetic_la_SOURCES = synthetic.c
libsynthetic_la_LIBADD = $(COMMON_LIBADD) -lm
pkglib_LTLIBRARIES += libsynthetic.la
dist_man7_MANS += Plugin_synthetic.man
endif
