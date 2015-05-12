SUBDIRS = . test

AM_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @EVENTINC@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@ -lpthread -lzap \
	     -levent -levent_pthreads -lcoll

libocm_la_SOURCES = ocm.c
lib_LTLIBRARIES = libocm.la

if ENABLE_OVIS_RCTRL
SUBDIRS += ovis_rctrl
endif

if ENABLE_OCMD
ocmd_SOURCES = ocmd.c
ocmd_LDADD = libocm.la -ldl -lovis_util
sbin_PROGRAMS = ocmd
SUBDIRS += plugins
endif

if ENABLE_PARSERS
SUBDIRS += parsers
endif

pkginclude_HEADERS = ocm.h
