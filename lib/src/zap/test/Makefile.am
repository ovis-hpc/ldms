ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = zap_test
AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)/src

zap_test_SOURCES = zap_test.c
zap_test_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_reconnect
zap_test_reconnect_SOURCES = zap_test_reconnect.c
zap_test_reconnect_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_disconnect
zap_test_disconnect_SOURCES = zap_test_disconnect.c
zap_test_disconnect_LDADD = $(builddir)/../libzap.la -lpthread -ldl
