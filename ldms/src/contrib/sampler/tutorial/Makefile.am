SUBDIRS =
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =

CORE = ../../../core
SAMPLER = ../../../sampler
LDMSD = ../../../ldmsd
BASE_SRC= $(srcdir)/$(SAMPLER)/sampler_base.c $(srcdir)/$(SAMPLER)/sampler_base.h

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../../.. -I$(srcdir)/$(SAMPLER) @OVIS_LIB_INCDIR_FLAG@ \
            -I$(srcdir)/$(LDMSD)

AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la @LDFLAGS_GETTIME@ -lovis_util -lcoll


if ENABLE_TUTORIAL_SAMPLER
libtutorial_sampler_la_SOURCES = tutorial_sampler.c $(BASE_SRC)
libtutorial_sampler_la_LIBADD = $(COMMON_LIBADD)
libtutorial_sampler_la_CFLAGS = $(AM_CFLAGS) 
pkglib_LTLIBRARIES += libtutorial_sampler.la
dist_man7_MANS += Plugin_tutorial_sampler.man
endif
EXTRA_DIST += Plugin_tutorial_sampler.man
