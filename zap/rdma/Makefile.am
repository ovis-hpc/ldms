lib_LTLIBRARIES = libzap_rdma.la

AM_CFLAGS = -Wall -g -I$(srcdir)/../src

libzap_rdma_la_SOURCES = src/zap_rdma.c src/zap_rdma.h
libzap_rdma_la_LIBADD = -libverbs -lrdmacm

install-exec-hook:
#	echo $(libdir) > $(libzap_rdma_conf)
#	ldconfig

uninstall-local:
#	rm -f $(libzap_rdma_conf)
#	ldconfig
