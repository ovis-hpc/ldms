pkglib_LTLIBRARIES =

CORE = ../core
AM_CPPFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ @LIBEVENT_INCDIR_FLAG@
AM_CFLAGS = $(INCLUDES)

if ENABLE_SOCK
libldmssock_la_SOURCES = ldms_sock.c ldms_sock_xprt.h
libldmssock_la_LIBADD = -levent -levent_pthreads -lcoll -lpthread \
			../core/libldms.la
libldmssock_la_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
libldmssock_la_LDFLAGS = $(AM_LDFLAGS) @LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
	@OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
pkglib_LTLIBRARIES += libldmssock.la
endif

if ENABLE_RDMA
libldmsrdma_la_SOURCES = ldms_rdma.c ldms_rdma_xprt.h
libldmsrdma_la_LIBADD = -libverbs -lrdmacm ../core/libldms.la \
			-lpthread
pkglib_LTLIBRARIES += libldmsrdma.la
endif

if ENABLE_UGNI
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
libldmsugni_la_CFLAGS = $(AM_CFLAGS) $(UGNI_CFLAGS) \
			@LIBEVENT_INCDIR_FLAG@ \
			@RCA_INCDIR_FLAG@ \
			@KRCA_INCDIR_FLAG@ \
			@CRAY_HSS_DEVEL_INCDIR_FLAG@
libldmsugni_la_LDFLAGS = $(UGNI_LIBS) @OVIS_LIB_LIB64DIR_FLAG@ \
			 @LIBEVENT_LIB64DIR_FLAG@ \
			 @OVIS_LIB_LIBDIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
			 @RCA_LIB64DIR_FLAG@ @RCA_LIBDIR_FLAG@ \
			-L../core
libldmsugni_la_SOURCES = ldms_ugni.c ldms_ugni.h
libldmsugni_la_LIBADD = -lldms -levent -levent_pthreads -lmmalloc -lrca -lpthread
pkglib_LTLIBRARIES += libldmsugni.la
endif

