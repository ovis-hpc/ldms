lib_LTLIBRARIES =

CORE = ../core
INCLUDES = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS = $(INCLUDES)

if ENABLE_SOCK
libldmssock_la_SOURCES = ldms_sock.c ldms_sock_xprt.h
libldmssock_la_CFLAGS = $(AM_CFLAGS) @EVENTINC@
libldmssock_la_LDFLAGS = $(AM_LDFLAGS) @EVENTLIBS@ -lpthread
libldmssock_la_LIBADD = ../core/libldms.la
lib_LTLIBRARIES += libldmssock.la
endif

if ENABLE_RDMA
libldmsrdma_la_SOURCES = ldms_rdma.c ldms_rdma_xprt.h
libldmsrdma_la_LIBADD = -libverbs -lrdmacm ../core/libldms.la
lib_LTLIBRARIES += libldmsrdma.la
endif

if ENABLE_UGNI
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
libldmsugni_la_CFLAGS = $(AM_CFLAGS) $(UGNI_CFLAGS) \
			@EVENTINC@
libldmsugni_la_LDFLAGS = $(UGNI_LIBS) @OVIS_LIB_LIBDIR_FLAG@ @EVENTLIBS@
libldmsugni_la_SOURCES = ldms_ugni.c ldms_ugni.h
libldmsugni_la_LIBADD = -lmmalloc
lib_LTLIBRARIES += libldmsugni.la
endif

