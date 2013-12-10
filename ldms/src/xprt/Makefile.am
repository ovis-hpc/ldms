lib_LTLIBRARIES =

CORE = ../core
INCLUDES = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ 
AM_CFLAGS = $(INCLUDES)

if ENABLE_SOCK
libldmssock_la_SOURCES = ldms_sock.c ldms_sock_xprt.h
libldmssock_la_LIBADD = -levent -levent_pthreads
libldmssock_la_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
libldmssock_la_LDFLAGS = $(AM_LDFLAGS) @LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@
lib_LTLIBRARIES += libldmssock.la
endif

if ENABLE_RDMA
libldmsrdma_la_SOURCES = ldms_rdma.c ldms_rdma_xprt.h
libldmsrdma_la_LIBADD = -libverbs -lrdmacm
lib_LTLIBRARIES += libldmsrdma.la
endif

if ENABLE_UGNI
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
libldmsugni_la_CFLAGS = $(AM_CFLAGS) $(UGNI_CFLAGS) \
			@LIBEVENT_INCDIR_FLAG@
libldmsugni_la_LDFLAGS = $(UGNI_LIBS) @OVIS_LIB_LIB64DIR_FLAG@ \
			 @LIBEVENT_LIB64DIR_FLAG@ \
			 @OVIS_LIB_LIBDIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@
libldmsugni_la_SOURCES = ldms_ugni.c ldms_ugni.h
libldmsugni_la_LIBADD = -levent -levent_pthreads -lmmalloc
lib_LTLIBRARIES += libldmsugni.la
endif

