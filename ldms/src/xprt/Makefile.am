lib_LTLIBRARIES =

CORE = ../core
INCLUDES = -I$(srcdir)/$(CORE)
AM_CFLAGS = $(INCLUDES)

if ENABLE_SOCK
libldmssock_la_SOURCES = ldms_sock.c
lib_LTLIBRARIES += libldmssock.la
endif

if ENABLE_RDMA
libldmsrdma_la_SOURCES = ldms_rdma.c
libldmsrdma_la_LIBADD = -libverbs -lrdmacm
lib_LTLIBRARIES += libldmsrdma.la
endif

if ENABLE_UGNI
PMI_CFLAGS = $(shell pkg-config --cflags cray-pmi)
PMI_LIBS = $(shell pkg-config --libs cray-pmi)
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
libldmsugni_la_CFLAGS = $(AM_CFLGAS) $(PMI_CFLAGS) $(UGNI_CFLAGS)
libldmsugni_la_LDFLAGS = $(PMI_LIBS) $(UGNI_LIBS)
libldmsugni_la_SOURCES = ldms_ugni.c
lib_LTLIBRARIES += libldmsugni.la
endif

