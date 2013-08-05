lib_LTLIBRARIES =

CORE = ../core
INCLUDES = -I$(srcdir)/$(CORE) -I@OVIS_LIB_INCDIR@
AM_CFLAGS = $(INCLUDES)

if ENABLE_SOCK
libldmssock_la_SOURCES = ldms_sock.c ldms_sock_xprt.h
libldmssock_la_LIBADD = -levent -levent_pthreads
libldmssock_la_CFLAGS = $(AM_CFLAGS) -I@LIBEVENT_INCDIR@
libldmssock_la_LDFLAGS = $(AM_LDFLAGS) -L@LIBEVENT_LIB64DIR@ -L@LIBEVENT_LIBDIR@
lib_LTLIBRARIES += libldmssock.la
endif

if ENABLE_RDMA
libldmsrdma_la_SOURCES = ldms_rdma.c ldms_rdma_xprt.h
libldmsrdma_la_LIBADD = -libverbs -lrdmacm
lib_LTLIBRARIES += libldmsrdma.la
endif

if ENABLE_UGNI
PMI_CFLAGS = $(shell pkg-config --cflags cray-pmi)
PMI_LIBS = $(shell pkg-config --libs cray-pmi)
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
libldmsugni_la_CFLAGS = $(AM_CFLGAS) $(PMI_CFLAGS) $(UGNI_CFLAGS) \
			-I@LIBEVENT_INCDIR@
libldmsugni_la_LDFLAGS = $(PMI_LIBS) $(UGNI_LIBS) -L@OVIS_LIB_LIB64DIR@ \
			 -L@LIBEVENT_LIB64DIR@ \
			 -L@OVIS_LIB_LIBDIR@ -L@LIBEVENT_LIBDIR@
libldmsugni_la_SOURCES = ldms_ugni.c ldms_ugni.h
libldmsugni_la_LIBADD = -levent -levent_pthreads -lmmalloc
lib_LTLIBRARIES += libldmsugni.la
endif

