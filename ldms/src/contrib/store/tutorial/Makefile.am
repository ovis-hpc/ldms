SUBDIRS =
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

CORE = ../../../core
SAMPLER = ../../../sampler
LDMSD = ../../../ldmsd

AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I../../.. @OVIS_LIB_INCDIR_FLAG@ \
	    -I$(srcdir)/$(LDMSD)

STORE_LIBADD = $(CORE)/libldms.la \
		-lcoll -lovis_util @OVIS_LIB_LIB64DIR_FLAG@ \
	       @OVIS_LIB_LIBDIR_FLAG@

ldmsstoreincludedir = $(includedir)/ldms
ldmsstoreinclude_HEADERS = 

if ENABLE_TUTORIAL_STORE
libstore_tutorial_la_SOURCES = store_tutorial.c
libstore_tutorial_la_CFLAGS = $(AM_CFLAGS)
libstore_tutorial_la_LIBADD = $(STORE_LIBADD)
pkglib_LTLIBRARIES += libstore_tutorial.la
dist_man7_MANS += Plugin_store_tutorial.man
endif

