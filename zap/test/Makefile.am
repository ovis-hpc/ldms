ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = src/zap_test
AM_CFLAGS = -Wall -g -I $(includedir)

src_zap_test_SOURCES = src/zap_test.c
src_zap_test_LDADD = $(libdir)/libzap.la -L$(libdir) -lpthread -ldl

install-exec-hook:
#	echo $(libdir) > $(libzap_rdma_conf)
#	ldconfig

uninstall-local:
#	rm -f $(libzap_rdma_conf)
#	ldconfig
