if ENABLE_MMAP
MMAP_FLAGS = -DENABLE_MMAP
endif

lib_LTLIBRARIES =

AM_CFLAGS = -Wall $(DBGFLAGS) -I$(top_srcdir)

ldmscoreincludedir = $(includedir)/ldms
ldmscoreinclude_HEADERS = ldms.h ldmsd.h ldms_xprt.h ogc_rbt.h 

libldms_la_SOURCES = ldms.c ldms_xprt.c ogc_rbt.c ldms_private.h
libldms_la_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ $(MMAP_FLAGS)
libldms_la_LIBADD = -ldl -lpthread @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ \
		    -lmmalloc -lcoll
lib_LTLIBRARIES += libldms.la

ldms_config.h: ../../config.h
	cp ../../config.h ldms_config.h

install-data-hook:
#	cp $(top_builddir)/scripts/ldmsd /etc/init.d/ldmsd
#	cp $(top_builddir)/scripts/schedstatd /etc/init.d/schedstatd
#	cp $(top_builddir)/scripts/meminfod /etc/init.d/meminfod
#	cp $(top_builddir)/scripts/sysclassibd /etc/init.d/procstatutil
