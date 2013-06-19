if ENABLE_MMAP
MMAP_FLAGS = -DENABLE_MMAP
endif

lib_LTLIBRARIES =

AM_CFLAGS = -Wall $(DBGFLAGS)

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldmsd.h ldms_xprt.h ogc_rbt.h

libldms_la_SOURCES = ldms.c ldms_xprt.c ogc_rbt.c ldms_private.h
libldms_la_CFLAGS = -I$(OVIS_LIB_INCDIR)
libldms_la_LIBADD = -ldl -lpthread -L$(OVIS_LIB_LIBDIR) -lmmalloc
lib_LTLIBRARIES += libldms.la

libldmsplugin_la_SOURCES = ldmsd_ctrl.c
lib_LTLIBRARIES += libldmsplugin.la

install-data-hook:
#	cp $(top_builddir)/scripts/ldmsd /etc/init.d/ldmsd
#	cp $(top_builddir)/scripts/schedstatd /etc/init.d/schedstatd
#	cp $(top_builddir)/scripts/meminfod /etc/init.d/meminfod
#	cp $(top_builddir)/scripts/sysclassibd /etc/init.d/procstatutil
