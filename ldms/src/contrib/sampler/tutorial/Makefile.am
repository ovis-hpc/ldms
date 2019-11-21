SUBDIRS =
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll


if ENABLE_TUTORIAL_SAMPLER
libtutorial_sampler_la_SOURCES = tutorial_sampler.c
libtutorial_sampler_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libtutorial_sampler.la
dist_man7_MANS += Plugin_tutorial_sampler.man
endif
EXTRA_DIST += Plugin_tutorial_sampler.man
