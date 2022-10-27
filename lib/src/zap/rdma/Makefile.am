pkglib_LTLIBRARIES = libzap_rdma.la

AM_CFLAGS = -I$(srcdir)/../.. -I$(srcdir)/.. -I$(top_srcdir) -I../..

libzap_rdma_la_SOURCES = zap_rdma.c zap_rdma.h
libzap_rdma_la_CFLAGS = @LIBIBVERBS_INCDIR_FLAG@ @LIBRDMACM_INCDIR_FLAG@ ${AM_CFLAGS}
libzap_rdma_la_LIBADD = -libverbs -lrdmacm -lpthread \
			 ../libzap.la ../../coll/libcoll.la \
			 ../../ovis_log/libovis_log.la \
			@LIBIBVERBS_LIBDIR_FLAG@ @LIBIBVERBS_LIB64DIR_FLAG@ \
			@LIBRDMACM_LIBDIR_FLAG@ @LIBRDMACM_LIB64DIR_FLAG@
