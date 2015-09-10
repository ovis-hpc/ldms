ACLOCAL_AMFLAGS = -I m4
CORE = ../core

sbin_PROGRAMS = test_ldms_xprt_reconnect

AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@

sbin_PROGRAMS += test_ldms_xprt_reconnect
test_ldms_xprt_reconnect_SOURCES = test_ldms_xprt_reconnect.c
test_ldms_xprt_reconnect_LDADD = $(CORE)/libldms.la
test_ldms_xprt_reconnect_LDFLAGS = $(AM_LDFLAGS) -lpthread
test_ldms_xprt_reconnect_CFLAGS = $(AM_CFLAGS)
if ENABLE_AUTH
test_ldms_xprt_reconnect_CFLAGS += -DENABLE_AUTH
endif
